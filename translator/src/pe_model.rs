// File: translator/src/pe_model.rs
// Purpose: Plain data describing a PE image to be written, plus layout constants shared by the relinker
// Author: Akilesh
// Date: 2026-10-28

/// Page size used for both PE section alignment and file alignment, so that a
/// section's file offset equals its RVA.
pub const PAGE_SIZE: u64 = 0x1000;
/// Preferred load address. The loader relocates the image when it cannot use it.
pub const IMAGE_BASE: u64 = 0x1_4000_0000;
/// ELF virtual addresses are shifted up by this much so the PE headers fit below the first segment.
pub const RVA_BIAS: u64 = PAGE_SIZE;

pub const SCN_CODE: u32 = 0x0000_0020;
pub const SCN_INITIALIZED_DATA: u32 = 0x0000_0040;
pub const SCN_DISCARDABLE: u32 = 0x0200_0000;
pub const SCN_EXECUTE: u32 = 0x2000_0000;
pub const SCN_READ: u32 = 0x4000_0000;
pub const SCN_WRITE: u32 = 0x8000_0000;

pub fn align_up(value: u64, alignment: u64) -> u64 {
    value.div_ceil(alignment) * alignment
}

pub fn align_down(value: u64, alignment: u64) -> u64 {
    value / alignment * alignment
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct DataDirectory {
    pub rva: u32,
    pub size: u32,
}

#[derive(Debug, Clone)]
pub struct PeSection {
    pub name: String,
    pub rva: u32,
    pub virtual_size: u32,
    pub data: Vec<u8>,
    pub characteristics: u32,
}

#[derive(Debug, Clone)]
pub struct PeImage {
    pub entry_rva: u32,
    pub sections: Vec<PeSection>,
    pub import_directory: DataDirectory,
    pub base_reloc_directory: DataDirectory,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn alignment_rounds_in_the_right_direction() {
        assert_eq!(align_up(0x1001, PAGE_SIZE), 0x2000);
        assert_eq!(align_up(0x1000, PAGE_SIZE), 0x1000);
        assert_eq!(align_down(0x1fff, PAGE_SIZE), 0x1000);
        assert_eq!(align_up(0, PAGE_SIZE), 0);
    }
}
