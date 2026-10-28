// File: translator/src/analyze/mod.rs
// Purpose: Parse a 64-bit x86-64 ET_DYN ELF (PIE or shared library) into an ElfInfo
// Author: Akilesh
// Date: 2026-10-28

mod address_map;
mod dynamic;
mod header;
mod relocs;
mod symbols;

use object::elf;
use object::read::elf::FileHeader;
use object::read::StringTable;
use object::Endianness;

use crate::elf_model::{ArrayRange, ElfInfo};
use crate::error::TranslateError;
use address_map::AddressMap;
use dynamic::DynamicTable;

pub use header::MAX_IMAGE_BYTES;

type Header = elf::FileHeader64<Endianness>;
type Phdr = elf::ProgramHeader64<Endianness>;
type Strings<'a> = StringTable<'a, &'a [u8]>;

const POINTER_SIZE: u64 = 8;

pub fn analyze(data: &[u8]) -> Result<ElfInfo, TranslateError> {
    let header = Header::parse(data).map_err(|e| malformed("ELF header", e))?;
    let endian = header.endian().map_err(|e| malformed("byte order", e))?;
    header::check_header(header, endian)?;
    let phdrs = header
        .program_headers(endian, data)
        .map_err(|e| malformed("program headers", e))?;
    header::reject_tls(phdrs, endian)?;
    let segments = header::load_segments(phdrs, endian)?;
    let map = AddressMap::new(&segments);
    let table = dynamic::read(phdrs, endian, data)?;
    let strings = table.strings(data, &map)?;
    let symbols = symbols::read(data, &map, &table, &strings)?;
    let relocations = relocs::read(data, &map, &table, symbols.len())?;
    Ok(ElfInfo {
        entry: header.e_entry(endian),
        soname: optional_string(&strings, table.soname)?,
        needed: needed_names(&strings, &table)?,
        symbols,
        relocations,
        init: table.init,
        fini: table.fini,
        init_array: array_range(table.init_array, table.init_arraysz),
        fini_array: array_range(table.fini_array, table.fini_arraysz),
        segments,
    })
}

fn malformed(what: &str, error: impl std::fmt::Display) -> TranslateError {
    TranslateError::Elf(format!("{what}: {error}"))
}

fn string_at(strings: &Strings<'_>, offset: u64) -> Result<String, TranslateError> {
    let offset = u32::try_from(offset)
        .map_err(|_| TranslateError::Elf("string offset is too large".into()))?;
    let bytes = strings
        .get(offset)
        .map_err(|()| TranslateError::Elf("string offset is outside the string table".into()))?;
    Ok(String::from_utf8_lossy(bytes).into_owned())
}

fn optional_string(
    strings: &Strings<'_>,
    offset: Option<u64>,
) -> Result<Option<String>, TranslateError> {
    offset.map(|offset| string_at(strings, offset)).transpose()
}

fn needed_names(
    strings: &Strings<'_>,
    table: &DynamicTable,
) -> Result<Vec<String>, TranslateError> {
    table
        .needed
        .iter()
        .map(|&offset| string_at(strings, offset))
        .collect()
}

fn array_range(vaddr: u64, size_in_bytes: u64) -> Option<ArrayRange> {
    let count = size_in_bytes / POINTER_SIZE;
    (vaddr != 0 && count != 0).then_some(ArrayRange { vaddr, count })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::elf_model::{Definition, RelocKind, SymbolBinding};

    const HELLO: &[u8] = include_bytes!("../../tests/fixtures/hello_dyn.elf");
    const TWO_LIBS: &[u8] = include_bytes!("../../tests/fixtures/two_libs/two_libs");
    const LIBMATH: &[u8] = include_bytes!("../../tests/fixtures/two_libs/libmath.so.1");
    const LIBGREET: &[u8] = include_bytes!("../../tests/fixtures/two_libs/libgreet.so.1");

    #[test]
    fn executable_reports_dependencies_entry_and_runpath_free_layout() {
        let info = analyze(TWO_LIBS).expect("fixture parses");
        assert_eq!(info.needed, ["libgreet.so.1", "libmath.so.1", "libc.so.6"]);
        assert_eq!(info.soname, None);
        assert_eq!(info.segments.len(), 4);
    }

    #[test]
    fn library_reports_soname_and_transitive_dependency() {
        let info = analyze(LIBGREET).expect("fixture parses");
        assert_eq!(info.soname.as_deref(), Some("libgreet.so.1"));
        assert_eq!(info.needed, ["libmath.so.1", "libc.so.6"]);
    }

    #[test]
    fn symbol_table_is_sized_from_the_hash_table() {
        let info = analyze(LIBMATH).expect("fixture parses");
        assert_eq!(
            info.symbols.len(),
            13,
            "readelf --dyn-syms lists 13 entries"
        );
        let exported: Vec<&str> = info
            .symbols
            .iter()
            .filter(|s| s.is_exported())
            .map(|s| s.name.as_str())
            .collect();
        for name in [
            "math_add",
            "math_mul",
            "math_bump",
            "math_format",
            "math_ops",
            "math_counter",
            "math_op_count",
        ] {
            assert!(exported.contains(&name), "{name} is exported");
        }
        assert_eq!(exported.len(), 7);
    }

    #[test]
    fn defined_data_symbols_keep_their_size_and_address() {
        let info = analyze(LIBMATH).expect("fixture parses");
        let counter = info
            .symbols
            .iter()
            .find(|s| s.name == "math_counter")
            .expect("present");
        assert_eq!(counter.size, 4);
        assert_eq!(counter.definition, Definition::Address(0x400c));
        assert_eq!(counter.binding, SymbolBinding::Global);
    }

    #[test]
    fn library_has_symbol_relocations_against_its_own_exports() {
        let info = analyze(LIBMATH).expect("fixture parses");
        let table_slots = info
            .relocations
            .iter()
            .filter(|r| matches!(r.kind, RelocKind::Symbol { symbol, .. } if info.symbols[symbol].name.starts_with("math_") && info.symbols[symbol].address().is_some()))
            .count();
        assert!(
            table_slots >= 3,
            "math_ops entries and the math_counter GOT slot"
        );
    }

    #[test]
    fn executable_has_a_copy_relocation_for_library_data() {
        let info = analyze(TWO_LIBS).expect("fixture parses");
        let copies: Vec<&str> = info
            .relocations
            .iter()
            .filter_map(|r| match r.kind {
                RelocKind::Copy { symbol } => Some(info.symbols[symbol].name.as_str()),
                _ => None,
            })
            .collect();
        assert_eq!(copies, ["math_counter"]);
    }

    #[test]
    fn init_and_fini_arrays_are_found() {
        let info = analyze(LIBMATH).expect("fixture parses");
        assert_eq!(info.init_array.map(|a| a.count), Some(2));
        assert_eq!(info.fini_array.map(|a| a.count), Some(2));
        assert!(info.init.is_some() && info.fini.is_some());
    }

    #[test]
    fn hello_still_parses() {
        let info = analyze(HELLO).expect("fixture parses");
        assert_eq!(info.entry, 0x1060);
        assert_eq!(info.needed, ["libc.so.6"]);
    }

    #[test]
    fn rejects_non_elf_input() {
        assert!(matches!(
            analyze(b"not an elf"),
            Err(TranslateError::Elf(_))
        ));
    }

    #[test]
    fn corrupted_inputs_never_panic() {
        for fixture in [HELLO, LIBMATH, TWO_LIBS] {
            for position in (0..fixture.len()).step_by(7) {
                let mut mutated = fixture.to_vec();
                mutated[position] ^= 0xFF;
                let _ = analyze(&mutated);
            }
            for length in (0..fixture.len()).step_by(61) {
                let _ = analyze(&fixture[..length]);
            }
        }
    }
}
