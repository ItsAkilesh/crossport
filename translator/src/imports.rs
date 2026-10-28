// File: translator/src/imports.rs
// Purpose: Build the PE import tables (descriptor, lookup table, IAT, names) for one shim DLL
// Author: Akilesh
// Date: 2026-10-28

use crate::pe_model::DataDirectory;

const DESCRIPTOR_SIZE: usize = 20;
const THUNK_SIZE: usize = 8;

pub struct ImportTables {
    pub data: Vec<u8>,
    pub directory: DataDirectory,
    iat_rvas: Vec<(String, u32)>,
}

impl ImportTables {
    /// RVA of the IAT slot the loader fills in for `name`.
    pub fn iat_rva(&self, name: &str) -> Option<u32> {
        self.iat_rvas.iter().find(|(n, _)| n == name).map(|&(_, rva)| rva)
    }
}

fn push_u32(out: &mut Vec<u8>, value: u32) {
    out.extend_from_slice(&value.to_le_bytes());
}

fn push_u64(out: &mut Vec<u8>, value: u64) {
    out.extend_from_slice(&value.to_le_bytes());
}

/// Lays out, in order: two descriptors (one plus the null terminator), the
/// lookup table, the IAT, the hint/name entries and the DLL name. `section_rva`
/// is where the returned bytes will be mapped.
pub fn build(dll_name: &str, names: &[String], section_rva: u32) -> ImportTables {
    let thunk_bytes = (names.len() + 1) * THUNK_SIZE;
    let lookup_offset = 2 * DESCRIPTOR_SIZE;
    let iat_offset = lookup_offset + thunk_bytes;
    let names_offset = iat_offset + thunk_bytes;
    let (hint_name_bytes, hint_name_offsets) = encode_hint_names(names, names_offset);
    let dll_name_offset = names_offset + hint_name_bytes.len();

    let rva = |offset: usize| section_rva + offset as u32;
    let mut data = Vec::new();
    push_u32(&mut data, rva(lookup_offset));
    push_u32(&mut data, 0);
    push_u32(&mut data, 0);
    push_u32(&mut data, rva(dll_name_offset));
    push_u32(&mut data, rva(iat_offset));
    data.resize(lookup_offset, 0);
    for _ in 0..2 {
        for &offset in &hint_name_offsets {
            push_u64(&mut data, u64::from(rva(offset)));
        }
        push_u64(&mut data, 0);
    }
    data.extend_from_slice(&hint_name_bytes);
    data.extend_from_slice(dll_name.as_bytes());
    data.push(0);

    let iat_rvas = names
        .iter()
        .enumerate()
        .map(|(i, name)| (name.clone(), rva(iat_offset + i * THUNK_SIZE)))
        .collect();
    ImportTables {
        data,
        directory: DataDirectory { rva: section_rva, size: (2 * DESCRIPTOR_SIZE) as u32 },
        iat_rvas,
    }
}

/// Returns the hint/name entries and each entry's offset within the section.
fn encode_hint_names(names: &[String], start_offset: usize) -> (Vec<u8>, Vec<usize>) {
    let mut bytes = Vec::new();
    let mut offsets = Vec::new();
    for name in names {
        offsets.push(start_offset + bytes.len());
        bytes.extend_from_slice(&[0, 0]);
        bytes.extend_from_slice(name.as_bytes());
        bytes.push(0);
        if bytes.len() % 2 != 0 {
            bytes.push(0);
        }
    }
    (bytes, offsets)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn names() -> Vec<String> {
        vec!["puts".to_string(), "crossport_start".to_string()]
    }

    #[test]
    fn iat_slots_are_eight_bytes_apart_and_findable() {
        let tables = build("shim.dll", &names(), 0x6000);
        let first = tables.iat_rva("puts").expect("puts has a slot");
        let second = tables.iat_rva("crossport_start").expect("start has a slot");
        assert_eq!(second - first, 8);
        assert_eq!(tables.iat_rva("missing"), None);
    }

    #[test]
    fn descriptor_points_at_lookup_table_and_iat() {
        let tables = build("shim.dll", &names(), 0x6000);
        let field = |at: usize| u32::from_le_bytes(tables.data[at..at + 4].try_into().expect("4 bytes"));
        assert_eq!(field(0), 0x6000 + 40);
        assert_eq!(field(16), tables.iat_rva("puts").expect("puts has a slot"));
        assert_eq!(&tables.data[40 + 2 * 8..40 + 3 * 8], &[0; 8], "lookup table ends with a null thunk");
        assert_eq!(tables.directory.rva, 0x6000);
    }

    #[test]
    fn dll_name_is_nul_terminated_at_the_end() {
        let tables = build("shim.dll", &names(), 0x6000);
        assert!(tables.data.ends_with(b"shim.dll\0"));
    }
}
