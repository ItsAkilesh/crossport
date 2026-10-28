// File: translator/src/rewrite/scan.rs
// Purpose: Find instructions with an FS-relative memory operand inside a module's real code ranges
// Author: Akilesh
// Date: 2026-10-28

use std::collections::BTreeMap;

use iced_x86::{Decoder, DecoderOptions, Instruction, OpKind, Register};

use crate::elf_model::{CodeRange, ElfInfo};
use crate::error::TranslateError;
use crate::pe_model::RVA_BIAS;

/// An instruction that reads or writes memory through `%fs`.
pub struct Site {
    /// Address in the PE image (the bias is already applied).
    pub rva: u64,
    pub instruction: Instruction,
}

/// Decodes only inside function ranges, so data bytes are never mistaken for code.
/// A module without `.eh_frame` falls back to its executable segments.
pub fn find_sites(image: &[u8], info: &ElfInfo) -> Result<Vec<Site>, TranslateError> {
    let mut sites: BTreeMap<u64, Site> = BTreeMap::new();
    for range in code_ranges(info) {
        let start = usize::try_from(RVA_BIAS + range.start)
            .map_err(|_| TranslateError::Elf("code range is too large".into()))?;
        let end = start
            .checked_add(range.len as usize)
            .ok_or_else(|| TranslateError::Elf("code range overflows".into()))?;
        let bytes = image
            .get(start..end)
            .ok_or_else(|| TranslateError::Elf("code range lies outside the image".into()))?;
        collect(bytes, start as u64, &mut sites);
    }
    Ok(sites.into_values().collect())
}

fn code_ranges(info: &ElfInfo) -> Vec<CodeRange> {
    if !info.code_ranges.is_empty() {
        return info.code_ranges.clone();
    }
    info.segments
        .iter()
        .filter(|segment| segment.flags.is_executable)
        .map(|segment| CodeRange {
            start: segment.vaddr,
            len: segment.file_size,
        })
        .collect()
}

fn collect(bytes: &[u8], ip: u64, sites: &mut BTreeMap<u64, Site>) {
    let mut decoder = Decoder::with_ip(64, bytes, ip, DecoderOptions::NONE);
    while decoder.can_decode() {
        let position = decoder.position();
        let instruction = decoder.decode();
        if instruction.is_invalid() {
            if decoder.set_position(position + 1).is_err() {
                return;
            }
            decoder.set_ip(ip + position as u64 + 1);
            continue;
        }
        if uses_fs_memory(&instruction) {
            sites.entry(instruction.ip()).or_insert(Site {
                rva: instruction.ip(),
                instruction,
            });
        }
    }
}

fn uses_fs_memory(instruction: &Instruction) -> bool {
    instruction.segment_prefix() == Register::FS
        && (0..instruction.op_count()).any(|operand| instruction.op_kind(operand) == OpKind::Memory)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::analyze::analyze;

    const TLS_BASIC: &[u8] = include_bytes!("../../tests/fixtures/tls_basic/tls_basic");

    fn image_of(data: &[u8], info: &ElfInfo) -> Vec<u8> {
        let end = info
            .segments
            .iter()
            .map(|s| s.vaddr + s.mem_size)
            .max()
            .unwrap_or(0);
        let mut image = vec![0u8; (RVA_BIAS + end) as usize + 0x1000];
        for segment in &info.segments {
            let source = &data
                [segment.file_offset as usize..(segment.file_offset + segment.file_size) as usize];
            let at = (RVA_BIAS + segment.vaddr) as usize;
            image[at..at + source.len()].copy_from_slice(source);
        }
        image
    }

    #[test]
    fn finds_every_fs_access_the_disassembler_listed() {
        let info = analyze(TLS_BASIC).expect("parses");
        let sites = find_sites(&image_of(TLS_BASIC, &info), &info).expect("scans");
        let vaddrs: Vec<u64> = sites.iter().map(|s| s.rva - RVA_BIAS).collect();
        for expected in [
            0x10adu64, 0x10c0, 0x10e0, 0x110a, 0x112c, 0x1155, 0x11a5, 0x11af, 0x1251, 0x1378,
            0x1437,
        ] {
            assert!(
                vaddrs.contains(&expected),
                "{expected:#x} missing from {vaddrs:x?}"
            );
        }
    }

    #[test]
    fn sites_are_unique_and_sorted() {
        let info = analyze(TLS_BASIC).expect("parses");
        let sites = find_sites(&image_of(TLS_BASIC, &info), &info).expect("scans");
        assert!(sites.windows(2).all(|pair| pair[0].rva < pair[1].rva));
    }

    #[test]
    fn a_range_outside_the_image_is_an_error() {
        let mut info = analyze(TLS_BASIC).expect("parses");
        info.code_ranges.push(CodeRange {
            start: 0x100_0000,
            len: 16,
        });
        assert!(find_sites(&image_of(TLS_BASIC, &info), &info).is_err());
    }
}
