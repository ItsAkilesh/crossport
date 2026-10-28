// File: translator/src/analyze.rs
// Purpose: Parse a 64-bit x86-64 PIE ELF into an ElfInfo (segments, dependencies, imports, relocations)
// Author: Akilesh
// Date: 2026-10-28

use std::collections::HashMap;

use object::elf;
use object::read::elf::{Dyn, FileHeader, ProgramHeader, Rela, Sym};
use object::read::{ReadRef, StringTable};
use object::Endianness;

use crate::elf_model::{
    ArrayRange, ElfInfo, ImportSymbol, LoadSegment, RelocKind, Relocation, SegmentFlags,
};
use crate::error::TranslateError;

type Header = elf::FileHeader64<Endianness>;
type Phdr = elf::ProgramHeader64<Endianness>;
type RelaEntry = elf::Rela64<Endianness>;
type SymEntry = elf::Sym64<Endianness>;

const SYMBOL_ENTRY_SIZE: u64 = 24;
const RELA_ENTRY_SIZE: u64 = 24;

/// Values read from the `PT_DYNAMIC` table. Addresses are virtual addresses.
#[derive(Default)]
struct DynamicTable {
    needed: Vec<u64>,
    strtab: u64,
    strsz: u64,
    symtab: u64,
    rela: u64,
    relasz: u64,
    jmprel: u64,
    pltrelsz: u64,
    init: Option<u64>,
    fini: Option<u64>,
    init_array: u64,
    init_arraysz: u64,
    fini_array: u64,
    fini_arraysz: u64,
}

pub fn analyze(data: &[u8]) -> Result<ElfInfo, TranslateError> {
    let header = Header::parse(data).map_err(|e| malformed("ELF header", e))?;
    let endian = header.endian().map_err(|e| malformed("byte order", e))?;
    check_header(header, endian)?;
    let phdrs = header
        .program_headers(endian, data)
        .map_err(|e| malformed("program headers", e))?;
    let segments = load_segments(phdrs, endian);
    let table = read_dynamic(phdrs, endian, data)?;
    let needed = read_needed(data, &segments, &table)?;
    let (imports, relocations) = read_relocations(data, &segments, &table)?;
    Ok(ElfInfo {
        entry: header.e_entry(endian),
        needed,
        imports,
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

fn check_header(header: &Header, endian: Endianness) -> Result<(), TranslateError> {
    if header.e_machine(endian) != elf::EM_X86_64 {
        return Err(TranslateError::Unsupported("only x86-64 ELF files are supported".into()));
    }
    if header.e_type(endian) != elf::ET_DYN {
        return Err(TranslateError::Unsupported(
            "only position-independent executables (ET_DYN) are supported".into(),
        ));
    }
    Ok(())
}

fn load_segments(phdrs: &[Phdr], endian: Endianness) -> Vec<LoadSegment> {
    phdrs
        .iter()
        .filter(|ph| ph.p_type(endian) == elf::PT_LOAD)
        .map(|ph| {
            let flags = ph.p_flags(endian);
            LoadSegment {
                vaddr: ph.p_vaddr(endian),
                file_offset: ph.p_offset(endian),
                file_size: ph.p_filesz(endian),
                mem_size: ph.p_memsz(endian),
                flags: SegmentFlags {
                    is_readable: flags.0 & elf::PF_R.0 != 0,
                    is_writable: flags.0 & elf::PF_W.0 != 0,
                    is_executable: flags.0 & elf::PF_X.0 != 0,
                },
            }
        })
        .collect()
}

fn read_dynamic(
    phdrs: &[Phdr],
    endian: Endianness,
    data: &[u8],
) -> Result<DynamicTable, TranslateError> {
    let entries = phdrs
        .iter()
        .find_map(|ph| ph.dynamic(endian, data).transpose())
        .ok_or_else(|| TranslateError::Elf("no PT_DYNAMIC segment".into()))?
        .map_err(|e| malformed("dynamic section", e))?;
    let mut table = DynamicTable::default();
    for entry in entries {
        let value = entry.d_val(endian);
        match entry.d_tag(endian) {
            elf::DT_NULL => break,
            elf::DT_NEEDED => table.needed.push(value),
            elf::DT_STRTAB => table.strtab = value,
            elf::DT_STRSZ => table.strsz = value,
            elf::DT_SYMTAB => table.symtab = value,
            elf::DT_RELA => table.rela = value,
            elf::DT_RELASZ => table.relasz = value,
            elf::DT_JMPREL => table.jmprel = value,
            elf::DT_PLTRELSZ => table.pltrelsz = value,
            elf::DT_INIT => table.init = Some(value),
            elf::DT_FINI => table.fini = Some(value),
            elf::DT_INIT_ARRAY => table.init_array = value,
            elf::DT_INIT_ARRAYSZ => table.init_arraysz = value,
            elf::DT_FINI_ARRAY => table.fini_array = value,
            elf::DT_FINI_ARRAYSZ => table.fini_arraysz = value,
            _ => {}
        }
    }
    Ok(table)
}

fn array_range(vaddr: u64, size_in_bytes: u64) -> Option<ArrayRange> {
    let count = size_in_bytes / 8;
    (vaddr != 0 && count != 0).then_some(ArrayRange { vaddr, count })
}

fn vaddr_to_offset(segments: &[LoadSegment], vaddr: u64) -> Result<u64, TranslateError> {
    segments
        .iter()
        .find(|s| vaddr >= s.vaddr && vaddr < s.vaddr + s.file_size)
        .map(|s| s.file_offset + (vaddr - s.vaddr))
        .ok_or_else(|| TranslateError::Elf(format!("address {vaddr:#x} is not backed by the file")))
}

fn string_table<'a>(
    data: &'a [u8],
    segments: &[LoadSegment],
    table: &DynamicTable,
) -> Result<StringTable<'a, &'a [u8]>, TranslateError> {
    let start = vaddr_to_offset(segments, table.strtab)?;
    Ok(StringTable::new(data, start, start + table.strsz))
}

fn read_needed(
    data: &[u8],
    segments: &[LoadSegment],
    table: &DynamicTable,
) -> Result<Vec<String>, TranslateError> {
    let strings = string_table(data, segments, table)?;
    table
        .needed
        .iter()
        .map(|&offset| {
            let bytes = strings
                .get(offset as u32)
                .map_err(|()| TranslateError::Elf("bad DT_NEEDED string offset".into()))?;
            Ok(String::from_utf8_lossy(bytes).into_owned())
        })
        .collect()
}

fn read_relocations(
    data: &[u8],
    segments: &[LoadSegment],
    table: &DynamicTable,
) -> Result<(Vec<ImportSymbol>, Vec<Relocation>), TranslateError> {
    let endian = Endianness::Little;
    let strings = string_table(data, segments, table)?;
    let symtab = vaddr_to_offset(segments, table.symtab)?;
    let mut imports = Vec::new();
    let mut import_index: HashMap<u32, usize> = HashMap::new();
    let mut relocations = Vec::new();
    for (vaddr, size) in [(table.rela, table.relasz), (table.jmprel, table.pltrelsz)] {
        for entry in read_rela_table(data, segments, vaddr, size)? {
            let kind = match entry.r_type(endian, false) {
                elf::R_X86_64_RELATIVE => RelocKind::Relative { addend: entry.r_addend(endian) },
                elf::R_X86_64_GLOB_DAT | elf::R_X86_64_JUMP_SLOT => {
                    let sym = entry.r_sym(endian, false);
                    let index = match import_index.get(&sym) {
                        Some(&index) => index,
                        None => {
                            imports.push(read_import(data, symtab, sym, &strings)?);
                            import_index.insert(sym, imports.len() - 1);
                            imports.len() - 1
                        }
                    };
                    RelocKind::Import { symbol: index }
                }
                other => {
                    return Err(TranslateError::Unsupported(format!(
                        "relocation type {other} (only RELATIVE, GLOB_DAT and JUMP_SLOT are translated)"
                    )))
                }
            };
            relocations.push(Relocation { offset: entry.r_offset(endian), kind });
        }
    }
    Ok((imports, relocations))
}

fn read_rela_table<'a>(
    data: &'a [u8],
    segments: &[LoadSegment],
    vaddr: u64,
    size: u64,
) -> Result<&'a [RelaEntry], TranslateError> {
    if vaddr == 0 || size == 0 {
        return Ok(&[]);
    }
    let offset = vaddr_to_offset(segments, vaddr)?;
    data.read_slice_at::<RelaEntry>(offset, (size / RELA_ENTRY_SIZE) as usize)
        .map_err(|()| TranslateError::Elf("relocation table is outside the file".into()))
}

fn read_import<'a>(
    data: &'a [u8],
    symtab_offset: u64,
    index: u32,
    strings: &StringTable<'a, &'a [u8]>,
) -> Result<ImportSymbol, TranslateError> {
    let endian = Endianness::Little;
    let offset = symtab_offset + u64::from(index) * SYMBOL_ENTRY_SIZE;
    let sym = data
        .read_at::<SymEntry>(offset)
        .map_err(|()| TranslateError::Elf("symbol table is outside the file".into()))?;
    if !sym.is_undefined(endian) {
        return Err(TranslateError::Unsupported(
            "relocation against a symbol defined in the executable itself".into(),
        ));
    }
    let name = strings
        .get(sym.st_name(endian))
        .map_err(|()| TranslateError::Elf("bad symbol name offset".into()))?;
    Ok(ImportSymbol {
        name: String::from_utf8_lossy(name).into_owned(),
        is_weak: sym.st_bind() == elf::STB_WEAK,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    const FIXTURE: &[u8] = include_bytes!("../tests/fixtures/hello_dyn.elf");

    #[test]
    fn reads_dependencies_and_entry() {
        let info = analyze(FIXTURE).expect("fixture parses");
        assert_eq!(info.needed, vec!["libc.so.6".to_string()]);
        assert_eq!(info.entry, 0x1060);
        assert_eq!(info.segments.len(), 4);
    }

    #[test]
    fn classifies_imports_by_binding() {
        let info = analyze(FIXTURE).expect("fixture parses");
        let strong: Vec<&str> =
            info.imports.iter().filter(|i| !i.is_weak).map(|i| i.name.as_str()).collect();
        assert_eq!(strong, vec!["__libc_start_main", "puts"]);
        assert_eq!(info.imports.iter().filter(|i| i.is_weak).count(), 4);
    }

    #[test]
    fn reads_relocations_and_init_arrays() {
        let info = analyze(FIXTURE).expect("fixture parses");
        let relative = info
            .relocations
            .iter()
            .filter(|r| matches!(r.kind, RelocKind::Relative { .. }))
            .count();
        assert_eq!(relative, 3);
        assert_eq!(info.relocations.len(), 9);
        assert_eq!(info.init_array, Some(ArrayRange { vaddr: 0x3de8, count: 1 }));
        assert_eq!(info.fini_array, Some(ArrayRange { vaddr: 0x3df0, count: 1 }));
    }

    #[test]
    fn rejects_non_elf_input() {
        assert!(matches!(analyze(b"not an elf"), Err(TranslateError::Elf(_))));
    }
}
