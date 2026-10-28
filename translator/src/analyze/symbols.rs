// File: translator/src/analyze/symbols.rs
// Purpose: Count and read .dynsym using only PT_DYNAMIC data (no section headers needed)
// Author: Akilesh
// Date: 2026-10-28

use object::elf;
use object::read::elf::Sym;
use object::read::ReadRef;
use object::Endianness;

use super::address_map::{add, mul, AddressMap};
use super::dynamic::DynamicTable;
use super::Strings;
use crate::elf_model::{Definition, DynSymbol, SymbolBinding};
use crate::error::TranslateError;

type SymEntry = elf::Sym64<Endianness>;

pub(super) const SYMBOL_ENTRY_SIZE: u64 = 24;
const MAX_SYMBOLS: u64 = 1 << 22;
const GNU_HASH_HEADER_SIZE: u64 = 16;
const GNU_HASH_BLOOM_WORD_SIZE: u64 = 8;
const GNU_HASH_CHAIN_END: u32 = 1;

fn read_u32(data: &[u8], offset: u64) -> Result<u32, TranslateError> {
    let bytes = data
        .read_bytes_at(offset, 4)
        .map_err(|()| TranslateError::Elf("hash table is outside the file".into()))?;
    let array: [u8; 4] = bytes
        .try_into()
        .map_err(|_| TranslateError::Elf("short hash table read".into()))?;
    Ok(u32::from_le_bytes(array))
}

/// `.dynsym` has no stored length; the hash table tells us how many symbols exist.
fn symbol_count(
    data: &[u8],
    map: &AddressMap<'_>,
    table: &DynamicTable,
) -> Result<u64, TranslateError> {
    if table.hash != 0 {
        let offset = map.file_offset(table.hash, 8)?;
        return Ok(u64::from(read_u32(data, add(offset, 4)?)?));
    }
    if table.gnu_hash != 0 {
        return gnu_hash_symbol_count(data, map, table.gnu_hash);
    }
    Err(TranslateError::Elf(
        "no DT_HASH or DT_GNU_HASH, cannot size .dynsym".into(),
    ))
}

fn gnu_hash_symbol_count(
    data: &[u8],
    map: &AddressMap<'_>,
    address: u64,
) -> Result<u64, TranslateError> {
    let header = map.file_offset(address, GNU_HASH_HEADER_SIZE)?;
    let bucket_count = u64::from(read_u32(data, header)?);
    let first_hashed = u64::from(read_u32(data, add(header, 4)?)?);
    let bloom_bytes = mul(
        u64::from(read_u32(data, add(header, 8)?)?),
        GNU_HASH_BLOOM_WORD_SIZE,
    )?;
    if bucket_count > MAX_SYMBOLS {
        return Err(TranslateError::LimitExceeded(
            "GNU hash bucket count".into(),
        ));
    }
    let buckets = add(add(header, GNU_HASH_HEADER_SIZE)?, bloom_bytes)?;
    let mut highest = 0u64;
    for index in 0..bucket_count {
        highest = highest.max(u64::from(read_u32(data, add(buckets, mul(index, 4)?)?)?));
    }
    if highest < first_hashed {
        return Ok(first_hashed);
    }
    let chains = add(buckets, mul(bucket_count, 4)?)?;
    let mut index = highest;
    loop {
        let link = read_u32(data, add(chains, mul(index - first_hashed, 4)?)?)?;
        index += 1;
        if link & GNU_HASH_CHAIN_END != 0 {
            return Ok(index);
        }
        if index > MAX_SYMBOLS {
            return Err(TranslateError::LimitExceeded("symbol count".into()));
        }
    }
}

pub(super) fn read(
    data: &[u8],
    map: &AddressMap<'_>,
    table: &DynamicTable,
    strings: &Strings<'_>,
) -> Result<Vec<DynSymbol>, TranslateError> {
    let count = symbol_count(data, map, table)?;
    if count > MAX_SYMBOLS {
        return Err(TranslateError::LimitExceeded(format!(
            "more than {MAX_SYMBOLS} dynamic symbols"
        )));
    }
    let base = map.file_offset(table.symtab, mul(count, SYMBOL_ENTRY_SIZE)?)?;
    let entries = data
        .read_slice_at::<SymEntry>(base, count as usize)
        .map_err(|()| TranslateError::Elf("symbol table is outside the file".into()))?;
    entries
        .iter()
        .map(|entry| to_symbol(entry, strings))
        .collect()
}

fn to_symbol(entry: &SymEntry, strings: &Strings<'_>) -> Result<DynSymbol, TranslateError> {
    let endian = Endianness::Little;
    let name = strings.get(entry.st_name(endian)).map_err(|()| {
        TranslateError::Elf("symbol name offset is outside the string table".into())
    })?;
    let shndx = entry.st_shndx(endian);
    let definition = match shndx {
        elf::SHN_UNDEF => Definition::Undefined,
        section if section.0 >= elf::SHN_LORESERVE => Definition::Absolute,
        _ => Definition::Address(entry.st_value(endian)),
    };
    let binding = match entry.st_bind() {
        elf::STB_LOCAL => SymbolBinding::Local,
        elf::STB_WEAK => SymbolBinding::Weak,
        _ => SymbolBinding::Global,
    };
    Ok(DynSymbol {
        name: String::from_utf8_lossy(name).into_owned(),
        binding,
        definition,
        size: entry.st_size(endian),
    })
}
