// File: translator/src/analyze/relocs.rs
// Purpose: Read RELA and JMPREL relocation tables into typed relocations, rejecting unsupported kinds
// Author: Akilesh
// Date: 2026-10-28

use object::elf;
use object::read::elf::Rela;
use object::read::ReadRef;
use object::Endianness;

use super::address_map::{mul, AddressMap};
use super::dynamic::DynamicTable;
use crate::elf_model::{RelocKind, Relocation};
use crate::error::TranslateError;

type RelaEntry = elf::Rela64<Endianness>;

const RELA_ENTRY_SIZE: u64 = 24;
const MAX_RELOCATIONS: u64 = 1 << 22;

pub(super) fn read(
    data: &[u8],
    map: &AddressMap<'_>,
    table: &DynamicTable,
    symbol_count: usize,
) -> Result<Vec<Relocation>, TranslateError> {
    let mut relocations = Vec::new();
    for (address, size) in [(table.rela, table.relasz), (table.jmprel, table.pltrelsz)] {
        for entry in read_table(data, map, address, size)? {
            if let Some(relocation) = convert(entry, symbol_count)? {
                relocations.push(relocation);
            }
        }
    }
    Ok(relocations)
}

fn read_table<'a>(
    data: &'a [u8],
    map: &AddressMap<'_>,
    address: u64,
    size: u64,
) -> Result<&'a [RelaEntry], TranslateError> {
    if address == 0 || size == 0 {
        return Ok(&[]);
    }
    let count = size / RELA_ENTRY_SIZE;
    if count > MAX_RELOCATIONS {
        return Err(TranslateError::LimitExceeded(format!(
            "more than {MAX_RELOCATIONS} relocations"
        )));
    }
    let offset = map.file_offset(address, mul(count, RELA_ENTRY_SIZE)?)?;
    data.read_slice_at::<RelaEntry>(offset, count as usize)
        .map_err(|()| TranslateError::Elf("relocation table is outside the file".into()))
}

fn convert(entry: &RelaEntry, symbol_count: usize) -> Result<Option<Relocation>, TranslateError> {
    let endian = Endianness::Little;
    let offset = entry.r_offset(endian);
    let addend = entry.r_addend(endian);
    let symbol = entry.r_sym(endian, false) as usize;
    let kind = match entry.r_type(endian, false) {
        elf::R_X86_64_NONE => return Ok(None),
        elf::R_X86_64_RELATIVE => RelocKind::Relative { addend },
        elf::R_X86_64_64 | elf::R_X86_64_GLOB_DAT | elf::R_X86_64_JUMP_SLOT => {
            check_symbol(symbol, symbol_count)?;
            let addend = if entry.r_type(endian, false) == elf::R_X86_64_64 {
                addend
            } else {
                0
            };
            RelocKind::Symbol { symbol, addend }
        }
        elf::R_X86_64_COPY => {
            check_symbol(symbol, symbol_count)?;
            RelocKind::Copy { symbol }
        }
        elf::R_X86_64_DTPMOD64 => RelocKind::TlsModule {
            symbol: optional_symbol(symbol, symbol_count)?,
        },
        elf::R_X86_64_DTPOFF64 => RelocKind::TlsOffset {
            symbol: optional_symbol(symbol, symbol_count)?,
            addend,
        },
        elf::R_X86_64_TPOFF64 => RelocKind::TlsThreadOffset {
            symbol: optional_symbol(symbol, symbol_count)?,
            addend,
        },
        elf::R_X86_64_TLSDESC => {
            return Err(TranslateError::Unsupported(
                "TLS descriptors (-mtls-dialect=gnu2) are not translated".into(),
            ))
        }
        other => {
            return Err(TranslateError::Unsupported(format!(
                "relocation type {}",
                other.0
            )));
        }
    };
    Ok(Some(Relocation { offset, kind }))
}

/// TLS relocations may omit the symbol (index 0), meaning "this module".
fn optional_symbol(symbol: usize, symbol_count: usize) -> Result<Option<usize>, TranslateError> {
    if symbol == 0 {
        return Ok(None);
    }
    check_symbol(symbol, symbol_count)?;
    Ok(Some(symbol))
}

fn check_symbol(symbol: usize, symbol_count: usize) -> Result<(), TranslateError> {
    if symbol == 0 || symbol >= symbol_count {
        return Err(TranslateError::Elf(format!(
            "relocation refers to symbol {symbol} of {symbol_count}"
        )));
    }
    Ok(())
}
