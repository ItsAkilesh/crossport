// File: translator/src/exports.rs
// Purpose: Encode a PE export directory: sorted name table, ordinals and address table
// Author: Akilesh
// Date: 2026-10-28

use crate::pe_model::DataDirectory;

const DIRECTORY_SIZE: usize = 40;
const ORDINAL_BASE: u32 = 1;

pub struct ExportEntry {
    pub name: String,
    pub rva: u32,
}

pub struct ExportTable {
    pub data: Vec<u8>,
    pub directory: DataDirectory,
}

fn push_u16(out: &mut Vec<u8>, value: u16) {
    out.extend_from_slice(&value.to_le_bytes());
}

fn push_u32(out: &mut Vec<u8>, value: u32) {
    out.extend_from_slice(&value.to_le_bytes());
}

/// Names must be sorted bytewise because the loader binary-searches them.
/// Duplicate names keep their first entry. `section_rva` is where the bytes are mapped.
pub fn build(dll_name: &str, mut entries: Vec<ExportEntry>, section_rva: u32) -> ExportTable {
    entries.sort_by(|left, right| left.name.as_bytes().cmp(right.name.as_bytes()));
    entries.dedup_by(|later, earlier| later.name == earlier.name);
    let count = entries.len();
    let functions_offset = DIRECTORY_SIZE;
    let names_offset = functions_offset + count * 4;
    let ordinals_offset = names_offset + count * 4;
    let strings_offset = ordinals_offset + count * 2;

    let mut strings = Vec::new();
    let mut name_rvas = Vec::new();
    for entry in &entries {
        name_rvas.push(section_rva + (strings_offset + strings.len()) as u32);
        strings.extend_from_slice(entry.name.as_bytes());
        strings.push(0);
    }
    let dll_name_rva = section_rva + (strings_offset + strings.len()) as u32;
    strings.extend_from_slice(dll_name.as_bytes());
    strings.push(0);

    let mut data = Vec::new();
    push_u32(&mut data, 0);
    push_u32(&mut data, 0);
    push_u16(&mut data, 0);
    push_u16(&mut data, 0);
    push_u32(&mut data, dll_name_rva);
    push_u32(&mut data, ORDINAL_BASE);
    push_u32(&mut data, count as u32);
    push_u32(&mut data, count as u32);
    push_u32(&mut data, section_rva + functions_offset as u32);
    push_u32(&mut data, section_rva + names_offset as u32);
    push_u32(&mut data, section_rva + ordinals_offset as u32);
    for entry in &entries {
        push_u32(&mut data, entry.rva);
    }
    for rva in &name_rvas {
        push_u32(&mut data, *rva);
    }
    for index in 0..count {
        push_u16(&mut data, index as u16);
    }
    data.extend_from_slice(&strings);
    let size = data.len() as u32;
    ExportTable {
        data,
        directory: DataDirectory {
            rva: section_rva,
            size,
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entry(name: &str, rva: u32) -> ExportEntry {
        ExportEntry {
            name: name.to_string(),
            rva,
        }
    }

    fn u32_at(bytes: &[u8], at: usize) -> u32 {
        u32::from_le_bytes(bytes[at..at + 4].try_into().expect("4 bytes"))
    }

    #[test]
    fn names_are_sorted_and_the_address_table_follows_them() {
        let table = build(
            "lib.dll",
            vec![entry("zeta", 0x3000), entry("alpha", 0x2000)],
            0x9000,
        );
        assert_eq!(u32_at(&table.data, 20), 2, "NumberOfFunctions");
        assert_eq!(u32_at(&table.data, 40), 0x2000, "alpha first");
        assert_eq!(u32_at(&table.data, 44), 0x3000);
    }

    #[test]
    fn directory_covers_the_whole_table_and_names_are_reachable() {
        let table = build("lib.dll", vec![entry("a", 1), entry("b", 2)], 0x9000);
        assert_eq!(table.directory.size as usize, table.data.len());
        let first_name_rva = u32_at(&table.data, 48) as usize;
        assert_eq!(
            &table.data[first_name_rva - 0x9000..first_name_rva - 0x9000 + 2],
            b"a\0"
        );
        let dll_name_rva = u32_at(&table.data, 12) as usize;
        assert_eq!(&table.data[dll_name_rva - 0x9000..], b"lib.dll\0");
    }

    #[test]
    fn duplicate_names_keep_one_entry() {
        let table = build("lib.dll", vec![entry("same", 1), entry("same", 2)], 0x9000);
        assert_eq!(u32_at(&table.data, 20), 1);
    }

    #[test]
    fn ordinals_are_the_sorted_positions() {
        let table = build(
            "lib.dll",
            vec![entry("b", 2), entry("a", 1), entry("c", 3)],
            0x9000,
        );
        let ordinals_at = 40 + 3 * 4 + 3 * 4;
        let ordinals: Vec<u16> = (0..3)
            .map(|i| {
                u16::from_le_bytes([
                    table.data[ordinals_at + 2 * i],
                    table.data[ordinals_at + 2 * i + 1],
                ])
            })
            .collect();
        assert_eq!(ordinals, [0, 1, 2]);
    }
}
