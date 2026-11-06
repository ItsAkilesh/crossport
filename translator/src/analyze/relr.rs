// File: translator/src/analyze/relr.rs
// Purpose: Expand a DT_RELR table (packed relative relocations) into the slots it relocates
// Author: Akilesh
// Date: 2026-11-06
//
// Newer toolchains (the Arch Linux ones, for example) pack the many `R_X86_64_RELATIVE` relocations into
// `.relr.dyn`: an even entry is the address of one slot to relocate, an odd entry is a bitmap whose bits 1..63
// mark the next 63 slots after the last address. The addend of each slot is the value already stored there.

use super::address_map::{mul, AddressMap};
use crate::elf_model::{RelocKind, Relocation};
use crate::error::TranslateError;

const WORD_BYTES: u64 = 8;
const BITMAP_BITS: u64 = 63;
const MAX_SLOTS: usize = 1 << 22;

/// The slot addresses a RELR table names, in table order.
pub(super) fn slots(words: &[u64]) -> Result<Vec<u64>, TranslateError> {
    let mut slots = Vec::new();
    let mut next: u64 = 0;
    for &word in words {
        if word & 1 == 0 {
            slots.push(word);
            next = word.wrapping_add(WORD_BYTES);
        } else {
            for bit in 0..BITMAP_BITS {
                if (word >> (bit + 1)) & 1 == 1 {
                    slots.push(next.wrapping_add(bit * WORD_BYTES));
                }
            }
            next = next.wrapping_add(BITMAP_BITS * WORD_BYTES);
        }
        if slots.len() > MAX_SLOTS {
            return Err(TranslateError::LimitExceeded(format!(
                "more than {MAX_SLOTS} RELR relocations"
            )));
        }
    }
    Ok(slots)
}

/// Reads the table at `address` (`size` bytes) and turns each slot into a `Relative` relocation.
pub(super) fn read(
    data: &[u8],
    map: &AddressMap<'_>,
    address: u64,
    size: u64,
) -> Result<Vec<Relocation>, TranslateError> {
    if address == 0 || size == 0 {
        return Ok(Vec::new());
    }
    let table = read_words(data, map, address, size / WORD_BYTES)?;
    let mut relocations = Vec::new();
    for slot in slots(&table)? {
        let addend = read_words(data, map, slot, 1)?[0] as i64;
        relocations.push(Relocation {
            offset: slot,
            kind: RelocKind::Relative { addend },
        });
    }
    Ok(relocations)
}

fn read_words(
    data: &[u8],
    map: &AddressMap<'_>,
    address: u64,
    count: u64,
) -> Result<Vec<u64>, TranslateError> {
    let length = mul(count, WORD_BYTES)?;
    let start = map.file_offset(address, length)? as usize;
    let bytes = data
        .get(start..start + length as usize)
        .ok_or_else(|| TranslateError::Elf("RELR data is outside the file".into()))?;
    Ok(bytes
        .chunks_exact(WORD_BYTES as usize)
        .map(|chunk| u64::from_le_bytes(chunk.try_into().expect("eight bytes")))
        .collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn an_even_entry_names_one_slot() {
        assert_eq!(slots(&[0x3000]).expect("expands"), vec![0x3000]);
    }

    #[test]
    fn a_bitmap_marks_the_slots_after_the_last_address() {
        let bitmap = (1u64 << 1) | (1 << 3) | 1;
        assert_eq!(
            slots(&[0x3000, bitmap]).expect("expands"),
            vec![0x3000, 0x3008, 0x3018]
        );
    }

    #[test]
    fn consecutive_bitmaps_advance_by_sixty_three_words() {
        let bitmap = (1u64 << 1) | 1;
        assert_eq!(
            slots(&[0x1000, bitmap, bitmap]).expect("expands"),
            vec![0x1000, 0x1008, 0x1000 + 8 + 63 * 8]
        );
    }
}
