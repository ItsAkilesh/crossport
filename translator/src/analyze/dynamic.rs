// File: translator/src/analyze/dynamic.rs
// Purpose: Read the PT_DYNAMIC table into a typed record of the addresses and values the analyzer needs
// Author: Akilesh
// Date: 2026-10-28

use object::elf;
use object::read::elf::{Dyn, ProgramHeader};
use object::read::StringTable;
use object::Endianness;

use super::address_map::AddressMap;
use super::Phdr;
use crate::error::TranslateError;

const RELA_ENTRY_SIZE: u64 = 24;
const MAX_NEEDED: usize = 256;

/// Values from `PT_DYNAMIC`. Addresses are virtual addresses; zero means absent.
#[derive(Default)]
pub(super) struct DynamicTable {
    pub needed: Vec<u64>,
    pub soname: Option<u64>,
    pub strtab: u64,
    pub strsz: u64,
    pub symtab: u64,
    pub hash: u64,
    pub gnu_hash: u64,
    pub rela: u64,
    pub relasz: u64,
    pub jmprel: u64,
    pub pltrelsz: u64,
    pub init: Option<u64>,
    pub fini: Option<u64>,
    pub init_array: u64,
    pub init_arraysz: u64,
    pub fini_array: u64,
    pub fini_arraysz: u64,
}

impl DynamicTable {
    pub(super) fn strings<'a>(
        &self,
        data: &'a [u8],
        map: &AddressMap<'_>,
    ) -> Result<StringTable<'a, &'a [u8]>, TranslateError> {
        let start = map.file_offset(self.strtab, self.strsz)?;
        Ok(StringTable::new(data, start, start + self.strsz))
    }
}

pub(super) fn read(phdrs: &[Phdr], endian: Endianness, data: &[u8]) -> Result<DynamicTable, TranslateError> {
    let entries = phdrs
        .iter()
        .find_map(|ph| ph.dynamic(endian, data).transpose())
        .ok_or_else(|| TranslateError::Elf("no PT_DYNAMIC segment".into()))?
        .map_err(|e| TranslateError::Elf(format!("dynamic section: {e}")))?;
    let mut table = DynamicTable::default();
    for entry in entries {
        let value = entry.d_val(endian);
        match entry.d_tag(endian) {
            elf::DT_NULL => break,
            elf::DT_NEEDED => push_needed(&mut table, value)?,
            elf::DT_SONAME => table.soname = Some(value),
            elf::DT_REL => return Err(unsupported("DT_REL relocations (only RELA is used on x86-64)")),
            elf::DT_RELAENT if value != RELA_ENTRY_SIZE => return Err(unsupported("unexpected RELA entry size")),
            tag => assign(&mut table, tag, value),
        }
    }
    Ok(table)
}

fn unsupported(message: &str) -> TranslateError {
    TranslateError::Unsupported(message.into())
}

fn push_needed(table: &mut DynamicTable, value: u64) -> Result<(), TranslateError> {
    if table.needed.len() >= MAX_NEEDED {
        return Err(TranslateError::LimitExceeded(format!("more than {MAX_NEEDED} DT_NEEDED entries")));
    }
    table.needed.push(value);
    Ok(())
}

fn assign(table: &mut DynamicTable, tag: elf::DynamicTag, value: u64) {
    match tag {
        elf::DT_STRTAB => table.strtab = value,
        elf::DT_STRSZ => table.strsz = value,
        elf::DT_SYMTAB => table.symtab = value,
        elf::DT_HASH => table.hash = value,
        elf::DT_GNU_HASH => table.gnu_hash = value,
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
