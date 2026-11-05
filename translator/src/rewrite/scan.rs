// File: translator/src/rewrite/scan.rs
// Purpose: Find instructions with an FS-relative memory operand inside a module's real code ranges
// Author: Akilesh
// Date: 2026-11-05

use std::collections::BTreeMap;

use iced_x86::{Code, Decoder, DecoderOptions, Instruction, OpKind, Register};

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
    for_each_instruction(image, info, &mut |instruction| {
        if uses_fs_memory(&instruction) {
            sites.entry(instruction.ip()).or_insert(Site {
                rva: instruction.ip(),
                instruction,
            });
        }
    })?;
    Ok(sites.into_values().collect())
}

/// How many raw `syscall` instructions the module's code holds. Linux system calls cannot run on Windows, so
/// each one is a place where the program would misbehave; the translator reports them.
pub fn count_syscalls(image: &[u8], info: &ElfInfo) -> Result<usize, TranslateError> {
    let mut count = 0;
    for_each_instruction(image, info, &mut |instruction| {
        count += usize::from(instruction.code() == Code::Syscall);
    })?;
    Ok(count)
}

fn for_each_instruction(
    image: &[u8],
    info: &ElfInfo,
    visit: &mut dyn FnMut(Instruction),
) -> Result<(), TranslateError> {
    for range in code_ranges(info) {
        let start = usize::try_from(RVA_BIAS + range.start)
            .map_err(|_| TranslateError::Elf("code range is too large".into()))?;
        let end = start
            .checked_add(range.len as usize)
            .ok_or_else(|| TranslateError::Elf("code range overflows".into()))?;
        let bytes = image
            .get(start..end)
            .ok_or_else(|| TranslateError::Elf("code range lies outside the image".into()))?;
        decode(bytes, start as u64, visit);
    }
    Ok(())
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

fn decode(bytes: &[u8], ip: u64, visit: &mut dyn FnMut(Instruction)) {
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
        visit(instruction);
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
    fn counts_the_syscall_instructions_in_the_code_ranges_only() {
        let mut info = analyze(TLS_BASIC).expect("parses");
        let mut image = image_of(TLS_BASIC, &info);
        let (inside, outside) = (0x200_0000u64, 0x200_0100u64);
        for address in [inside, inside + 2, outside] {
            let at = (RVA_BIAS + address) as usize;
            image.resize(image.len().max(at + 4), 0);
            image[at..at + 2].copy_from_slice(&[0x0f, 0x05]);
        }
        info.code_ranges = vec![CodeRange {
            start: inside,
            len: 4,
        }];
        assert_eq!(count_syscalls(&image, &info).expect("scans"), 2);
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
