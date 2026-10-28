// File: translator/src/base_reloc.rs
// Purpose: Encode PE base relocation blocks (IMAGE_REL_BASED_DIR64) for 64-bit pointer slots
// Author: Akilesh
// Date: 2026-10-28

use crate::pe_model::PAGE_SIZE;

const TYPE_DIR64: u16 = 10;
const BLOCK_HEADER_SIZE: u32 = 8;
const ENTRY_SIZE: u32 = 2;
const PAGE_OFFSET_MASK: u32 = (PAGE_SIZE as u32) - 1;

/// Encodes one block per 4 KiB page. `slot_rvas` are the RVAs of 64-bit
/// pointers the loader must adjust when the image moves.
pub fn build(slot_rvas: &[u32]) -> Vec<u8> {
    let mut sorted = slot_rvas.to_vec();
    sorted.sort_unstable();
    sorted.dedup();
    let mut out = Vec::new();
    let mut index = 0;
    while index < sorted.len() {
        let page = sorted[index] & !PAGE_OFFSET_MASK;
        let end = sorted[index..]
            .iter()
            .position(|&rva| rva & !PAGE_OFFSET_MASK != page)
            .map_or(sorted.len(), |count| index + count);
        encode_block(&mut out, page, &sorted[index..end]);
        index = end;
    }
    out
}

fn encode_block(out: &mut Vec<u8>, page: u32, rvas: &[u32]) {
    let needs_padding = rvas.len() & 1 == 1;
    let entry_count = rvas.len() + usize::from(needs_padding);
    let size = BLOCK_HEADER_SIZE + entry_count as u32 * ENTRY_SIZE;
    out.extend_from_slice(&page.to_le_bytes());
    out.extend_from_slice(&size.to_le_bytes());
    for &rva in rvas {
        let entry = (TYPE_DIR64 << 12) | (rva & PAGE_OFFSET_MASK) as u16;
        out.extend_from_slice(&entry.to_le_bytes());
    }
    if needs_padding {
        out.extend_from_slice(&0u16.to_le_bytes());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_input_produces_no_blocks() {
        assert!(build(&[]).is_empty());
    }

    #[test]
    fn same_page_slots_share_one_padded_block() {
        let bytes = build(&[0x5008, 0x5000, 0x5010]);
        assert_eq!(&bytes[0..4], &0x5000u32.to_le_bytes());
        assert_eq!(&bytes[4..8], &16u32.to_le_bytes(), "3 entries padded to 4");
        assert_eq!(&bytes[8..10], &0xA000u16.to_le_bytes());
        assert_eq!(&bytes[10..12], &0xA008u16.to_le_bytes());
        assert_eq!(&bytes[14..16], &0u16.to_le_bytes(), "padding entry");
        assert_eq!(bytes.len(), 16);
    }

    #[test]
    fn slots_on_different_pages_get_separate_blocks() {
        let bytes = build(&[0x4ff8, 0x5000]);
        assert_eq!(bytes.len(), 2 * 12);
        assert_eq!(&bytes[12..16], &0x5000u32.to_le_bytes());
    }

    #[test]
    fn duplicate_slots_are_emitted_once() {
        assert_eq!(build(&[0x5000, 0x5000]).len(), 12);
    }
}
