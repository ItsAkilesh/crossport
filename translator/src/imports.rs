// File: translator/src/imports.rs
// Purpose: Collect the symbols a module imports per DLL and encode the PE import tables (descriptors, lookup tables, IAT)
// Author: Akilesh
// Date: 2026-10-28

use std::collections::HashMap;

use crate::pe_model::DataDirectory;

const DESCRIPTOR_SIZE: usize = 20;
const THUNK_SIZE: usize = 8;

/// Builder that records each `(dll, symbol)` pair once, in first-seen order.
#[derive(Default)]
pub struct ImportSet {
    dlls: Vec<ImportDll>,
    dll_index: HashMap<String, usize>,
}

struct ImportDll {
    name: String,
    symbols: Vec<String>,
    symbol_index: HashMap<String, usize>,
}

impl ImportSet {
    pub fn add(&mut self, dll: &str, symbol: &str) {
        let dll_position = match self.dll_index.get(dll) {
            Some(&position) => position,
            None => {
                self.dlls.push(ImportDll {
                    name: dll.to_string(),
                    symbols: Vec::new(),
                    symbol_index: HashMap::new(),
                });
                self.dll_index.insert(dll.to_string(), self.dlls.len() - 1);
                self.dlls.len() - 1
            }
        };
        let entry = &mut self.dlls[dll_position];
        if !entry.symbol_index.contains_key(symbol) {
            entry
                .symbol_index
                .insert(symbol.to_string(), entry.symbols.len());
            entry.symbols.push(symbol.to_string());
        }
    }
}

pub struct ImportTables {
    pub data: Vec<u8>,
    pub directory: DataDirectory,
    iat_rvas: HashMap<(String, String), u32>,
}

impl ImportTables {
    /// RVA of the IAT slot the loader fills in for `symbol` imported from `dll`.
    pub fn iat_rva(&self, dll: &str, symbol: &str) -> Option<u32> {
        self.iat_rvas
            .get(&(dll.to_string(), symbol.to_string()))
            .copied()
    }
}

fn push_u32(out: &mut Vec<u8>, value: u32) {
    out.extend_from_slice(&value.to_le_bytes());
}

fn push_u64(out: &mut Vec<u8>, value: u64) {
    out.extend_from_slice(&value.to_le_bytes());
}

/// Section layout, in order: one descriptor per DLL plus a null terminator,
/// then per DLL a lookup table and an IAT (each null terminated), the
/// hint/name entries, and the DLL names. `section_rva` is where the bytes are mapped.
pub fn build(set: &ImportSet, section_rva: u32) -> ImportTables {
    let descriptor_bytes = (set.dlls.len() + 1) * DESCRIPTOR_SIZE;
    let mut cursor = descriptor_bytes;
    let mut thunks = Vec::new();
    for dll in &set.dlls {
        let size = (dll.symbols.len() + 1) * THUNK_SIZE;
        thunks.push((cursor, cursor + size));
        cursor += 2 * size;
    }
    let (name_bytes, name_offsets, dll_name_offsets) = encode_names(set, cursor);
    let rva = |offset: usize| section_rva + offset as u32;

    let mut data = Vec::new();
    for index in 0..set.dlls.len() {
        push_u32(&mut data, rva(thunks[index].0));
        push_u32(&mut data, 0);
        push_u32(&mut data, 0);
        push_u32(&mut data, rva(dll_name_offsets[index]));
        push_u32(&mut data, rva(thunks[index].1));
    }
    data.resize(descriptor_bytes, 0);
    let mut iat_rvas = HashMap::new();
    for (index, dll) in set.dlls.iter().enumerate() {
        for _ in 0..2 {
            for offset in &name_offsets[index] {
                push_u64(&mut data, u64::from(rva(*offset)));
            }
            push_u64(&mut data, 0);
        }
        for (slot, symbol) in dll.symbols.iter().enumerate() {
            iat_rvas.insert(
                (dll.name.clone(), symbol.clone()),
                rva(thunks[index].1 + slot * THUNK_SIZE),
            );
        }
    }
    data.extend_from_slice(&name_bytes);
    ImportTables {
        data,
        directory: DataDirectory {
            rva: section_rva,
            size: descriptor_bytes as u32,
        },
        iat_rvas,
    }
}

type EncodedNames = (Vec<u8>, Vec<Vec<usize>>, Vec<usize>);

/// Returns the encoded hint/name entries and DLL names, the offset of each
/// symbol's entry (per DLL) and the offset of each DLL name.
fn encode_names(set: &ImportSet, start_offset: usize) -> EncodedNames {
    let mut bytes = Vec::new();
    let mut symbol_offsets = Vec::new();
    for dll in &set.dlls {
        let mut offsets = Vec::new();
        for symbol in &dll.symbols {
            offsets.push(start_offset + bytes.len());
            bytes.extend_from_slice(&[0, 0]);
            bytes.extend_from_slice(symbol.as_bytes());
            bytes.push(0);
            if bytes.len() % 2 != 0 {
                bytes.push(0);
            }
        }
        symbol_offsets.push(offsets);
    }
    let mut dll_offsets = Vec::new();
    for dll in &set.dlls {
        dll_offsets.push(start_offset + bytes.len());
        bytes.extend_from_slice(dll.name.as_bytes());
        bytes.push(0);
    }
    (bytes, symbol_offsets, dll_offsets)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn set() -> ImportSet {
        let mut set = ImportSet::default();
        set.add("shim.dll", "puts");
        set.add("libm.so.1.dll", "math_add");
        set.add("shim.dll", "crossport_start");
        set.add("shim.dll", "puts");
        set
    }

    fn u32_at(bytes: &[u8], at: usize) -> u32 {
        u32::from_le_bytes(bytes[at..at + 4].try_into().expect("4 bytes"))
    }

    #[test]
    fn duplicate_pairs_collapse_and_slots_are_eight_bytes_apart() {
        let tables = build(&set(), 0x6000);
        let first = tables.iat_rva("shim.dll", "puts").expect("puts has a slot");
        let second = tables
            .iat_rva("shim.dll", "crossport_start")
            .expect("start has a slot");
        assert_eq!(second - first, 8);
        assert!(
            tables.iat_rva("shim.dll", "math_add").is_none(),
            "same name, other DLL"
        );
        assert!(tables.iat_rva("libm.so.1.dll", "math_add").is_some());
    }

    #[test]
    fn descriptors_point_at_their_own_lookup_tables_and_iats() {
        let tables = build(&set(), 0x6000);
        let first_lookup = u32_at(&tables.data, 0);
        let first_iat = u32_at(&tables.data, 16);
        assert_eq!(
            first_iat - first_lookup,
            3 * 8,
            "two symbols plus the null thunk"
        );
        assert_eq!(first_iat, tables.iat_rva("shim.dll", "puts").expect("slot"));
        assert_eq!(
            tables.directory.size,
            3 * 20,
            "two DLLs plus the null descriptor"
        );
        assert_eq!(
            &tables.data[40..60],
            &[0; 20],
            "descriptor list is null terminated"
        );
    }

    #[test]
    fn dll_names_are_nul_terminated() {
        let tables = build(&set(), 0x6000);
        assert!(tables.data.ends_with(b"libm.so.1.dll\0"));
    }

    #[test]
    fn an_empty_set_still_has_a_terminator() {
        let tables = build(&ImportSet::default(), 0x6000);
        assert_eq!(tables.data, vec![0; 20]);
    }
}
