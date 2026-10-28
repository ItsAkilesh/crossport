// File: translator/src/relink/layout.rs
// Purpose: Place ELF PT_LOAD segments in the PE address space and build the module's virtual image
// Author: Akilesh
// Date: 2026-10-28

use crate::elf_model::{ElfInfo, LoadSegment};
use crate::error::TranslateError;
use crate::pe_model::{
    align_down, align_up, PeSection, PAGE_SIZE, RVA_BIAS, SCN_CODE, SCN_EXECUTE,
    SCN_INITIALIZED_DATA, SCN_READ, SCN_WRITE,
};

/// A PT_LOAD segment placed in the PE address space.
pub(super) struct Placement {
    pub rva: u64,
    pub file_len: u64,
    pub mem_len: u64,
    pub flags: u32,
}

pub(super) struct Layout {
    pub placements: Vec<Placement>,
    /// First page-aligned RVA after every segment.
    pub end_rva: u64,
}

/// Each segment becomes one section at `RVA_BIAS + page-aligned vaddr`. The
/// uniform shift keeps every RIP-relative reference between segments valid.
pub(super) fn plan(segments: &[LoadSegment]) -> Result<Layout, TranslateError> {
    let mut placements: Vec<Placement> = Vec::with_capacity(segments.len());
    for segment in segments {
        let start = align_down(segment.vaddr, PAGE_SIZE);
        let rva = RVA_BIAS + start;
        if let Some(previous) = placements.last() {
            if rva < previous.rva + align_up(previous.mem_len, PAGE_SIZE) {
                return Err(TranslateError::Unsupported(
                    "two segments share a page".into(),
                ));
            }
        }
        placements.push(Placement {
            rva,
            file_len: segment.vaddr + segment.file_size - start,
            mem_len: segment.vaddr + segment.mem_size - start,
            flags: characteristics(segment),
        });
    }
    let end_rva = placements
        .iter()
        .map(|p| p.rva + align_up(p.mem_len, PAGE_SIZE))
        .max()
        .unwrap_or(RVA_BIAS);
    Ok(Layout {
        placements,
        end_rva,
    })
}

fn characteristics(segment: &LoadSegment) -> u32 {
    let mut flags = SCN_INITIALIZED_DATA;
    if segment.flags.is_readable {
        flags |= SCN_READ;
    }
    if segment.flags.is_writable {
        flags |= SCN_WRITE;
    }
    if segment.flags.is_executable {
        flags |= SCN_EXECUTE | SCN_CODE;
    }
    flags
}

/// Allocates the image (`end_rva` bytes, so index == RVA) and copies each segment's file bytes into place.
pub(super) fn build_image(
    info: &ElfInfo,
    data: &[u8],
    layout: &Layout,
) -> Result<Vec<u8>, TranslateError> {
    let mut image = vec![0u8; layout.end_rva as usize];
    for segment in &info.segments {
        let start = segment.file_offset as usize;
        let source = data
            .get(start..start + segment.file_size as usize)
            .ok_or_else(|| {
                TranslateError::Elf("segment extends past the end of the file".into())
            })?;
        let at = (RVA_BIAS + segment.vaddr) as usize;
        image[at..at + source.len()].copy_from_slice(source);
    }
    Ok(image)
}

pub(super) fn elf_sections(layout: &Layout, image: &[u8]) -> Vec<PeSection> {
    layout
        .placements
        .iter()
        .enumerate()
        .map(|(index, placement)| {
            let start = placement.rva as usize;
            let raw_len = align_up(placement.file_len, PAGE_SIZE) as usize;
            PeSection {
                name: format!(".elf{index}"),
                rva: placement.rva as u32,
                virtual_size: placement.mem_len as u32,
                data: image[start..start + raw_len].to_vec(),
                characteristics: placement.flags,
            }
        })
        .collect()
}

pub(super) fn write_u64(image: &mut [u8], rva: u64, value: u64) -> Result<(), TranslateError> {
    let end = rva.checked_add(8).ok_or_else(|| outside(rva))?;
    let slot = image
        .get_mut(rva as usize..end as usize)
        .ok_or_else(|| outside(rva))?;
    slot.copy_from_slice(&value.to_le_bytes());
    Ok(())
}

fn outside(rva: u64) -> TranslateError {
    TranslateError::Elf(format!("relocation target {rva:#x} is outside the image"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::elf_model::SegmentFlags;

    fn segment(vaddr: u64, file_size: u64, mem_size: u64, is_writable: bool) -> LoadSegment {
        LoadSegment {
            vaddr,
            file_offset: vaddr,
            file_size,
            mem_size,
            flags: SegmentFlags {
                is_readable: true,
                is_writable,
                is_executable: false,
            },
        }
    }

    #[test]
    fn segments_are_shifted_by_the_bias_and_page_aligned() {
        let layout = plan(&[
            segment(0, 0x600, 0x600, false),
            segment(0x3de8, 0x228, 0x230, true),
        ])
        .expect("plans");
        assert_eq!(layout.placements[0].rva, 0x1000);
        assert_eq!(layout.placements[1].rva, 0x4000);
        assert_eq!(layout.placements[1].mem_len, 0xde8 + 0x230);
        assert_eq!(layout.end_rva, 0x5000 + 0x1000);
    }

    #[test]
    fn segments_sharing_a_page_are_rejected() {
        let result = plan(&[
            segment(0, 0x1800, 0x1800, false),
            segment(0x1900, 0x10, 0x10, true),
        ]);
        assert!(matches!(result, Err(TranslateError::Unsupported(_))));
    }

    #[test]
    fn write_u64_is_bounds_checked() {
        let mut image = vec![0u8; 16];
        write_u64(&mut image, 8, 0x1122_3344_5566_7788).expect("fits");
        assert_eq!(&image[8..], &0x1122_3344_5566_7788u64.to_le_bytes());
        assert!(write_u64(&mut image, 9, 0).is_err());
        assert!(write_u64(&mut image, u64::MAX, 0).is_err());
    }
}
