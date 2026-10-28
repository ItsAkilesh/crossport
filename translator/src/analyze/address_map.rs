// File: translator/src/analyze/address_map.rs
// Purpose: Map virtual addresses to file offsets with checked arithmetic; the single place offsets are computed
// Author: Akilesh
// Date: 2026-10-28

use crate::elf_model::LoadSegment;
use crate::error::TranslateError;

fn overflow() -> TranslateError {
    TranslateError::Elf("offset arithmetic overflows".into())
}

pub(super) fn add(left: u64, right: u64) -> Result<u64, TranslateError> {
    left.checked_add(right).ok_or_else(overflow)
}

pub(super) fn mul(left: u64, right: u64) -> Result<u64, TranslateError> {
    left.checked_mul(right).ok_or_else(overflow)
}

pub(super) struct AddressMap<'a> {
    segments: &'a [LoadSegment],
}

impl<'a> AddressMap<'a> {
    pub(super) fn new(segments: &'a [LoadSegment]) -> Self {
        Self { segments }
    }

    /// File offset of `[vaddr, vaddr + len)`, which must lie entirely inside
    /// the file-backed bytes of one `PT_LOAD` segment.
    pub(super) fn file_offset(&self, vaddr: u64, len: u64) -> Result<u64, TranslateError> {
        let end = add(vaddr, len)?;
        for segment in self.segments {
            let segment_end = add(segment.vaddr, segment.file_size)?;
            if vaddr >= segment.vaddr && end <= segment_end {
                return add(segment.file_offset, vaddr - segment.vaddr);
            }
        }
        Err(TranslateError::Elf(format!(
            "address range {vaddr:#x}+{len:#x} is not backed by the file"
        )))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::elf_model::SegmentFlags;

    fn segment(vaddr: u64, file_offset: u64, file_size: u64) -> LoadSegment {
        LoadSegment {
            vaddr,
            file_offset,
            file_size,
            mem_size: file_size,
            flags: SegmentFlags {
                is_readable: true,
                is_writable: false,
                is_executable: false,
            },
        }
    }

    #[test]
    fn maps_addresses_inside_a_segment() {
        let segments = [segment(0x1000, 0x200, 0x100)];
        let map = AddressMap::new(&segments);
        assert_eq!(map.file_offset(0x1010, 8).expect("inside"), 0x210);
        assert_eq!(
            map.file_offset(0x1000, 0x100).expect("whole segment"),
            0x200
        );
    }

    #[test]
    fn rejects_ranges_that_leave_the_segment_or_overflow() {
        let segments = [segment(0x1000, 0x200, 0x100)];
        let map = AddressMap::new(&segments);
        assert!(map.file_offset(0x10f9, 8).is_err());
        assert!(map.file_offset(0x900, 8).is_err());
        assert!(map.file_offset(u64::MAX - 2, 8).is_err());
    }
}
