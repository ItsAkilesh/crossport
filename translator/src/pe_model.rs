// File: translator/src/pe_model.rs
// Purpose: Plain data describing a PE image to be written, plus layout constants shared by the relinker
// Author: Akilesh
// Date: 2026-10-28

/// Page size used for both PE section alignment and file alignment, so that a
/// section's file offset equals its RVA.
pub const PAGE_SIZE: u64 = 0x1000;
/// ELF virtual addresses are shifted up by this much so the PE headers fit below the first segment.
pub const RVA_BIAS: u64 = PAGE_SIZE;

/// Preferred load address of the executable. The loader relocates any image it cannot place there.
pub const EXECUTABLE_BASE: u64 = 0x1_4000_0000;
const LIBRARY_BASE: u64 = 0x1_8000_0000;
/// Spacing between preferred library bases; larger than the biggest allowed module image.
const LIBRARY_SPACING: u64 = 0x1000_0000;

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

/// Preferred image base for the module at `index` (0 is the executable).
pub fn preferred_base(index: usize) -> u64 {
    if index == 0 {
        EXECUTABLE_BASE
    } else {
        LIBRARY_BASE + (index as u64 - 1) * LIBRARY_SPACING
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ImageKind {
    Executable,
    Dll,
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
    pub kind: ImageKind,
    pub image_base: u64,
    /// Zero for a DLL: its code is entered through exports, never through a loader entry point.
    pub entry_rva: u32,
    pub sections: Vec<PeSection>,
    pub import_directory: DataDirectory,
    pub export_directory: DataDirectory,
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

    #[test]
    fn library_bases_never_overlap_each_other_or_the_executable() {
        assert_eq!(preferred_base(0), EXECUTABLE_BASE);
        assert!(preferred_base(1) > EXECUTABLE_BASE + 0x1000_0000);
        assert_eq!(preferred_base(2) - preferred_base(1), LIBRARY_SPACING);
    }
}
