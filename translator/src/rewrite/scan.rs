// File: translator/src/rewrite/scan.rs
// Purpose: Find instructions with an FS-relative memory operand inside a module's real code ranges
// Author: Akilesh
// Date: 2026-11-05

use std::collections::{BTreeMap, HashSet};

use iced_x86::{
    Code, Decoder, DecoderOptions, FlowControl, Instruction, Mnemonic, OpKind, Register,
};

use crate::elf_model::{CodeRange, ElfInfo};
use crate::error::TranslateError;
use crate::pe_model::RVA_BIAS;

/// An instruction that reads or writes memory through `%fs`.
pub struct Site {
    /// Address in the PE image (the bias is already applied).
    pub rva: u64,
    pub instruction: Instruction,
    /// For an instruction shorter than a jump: the straight-line run of instructions around it (itself
    /// included) that moves into the stub together, long enough for a jump. Empty when none is safe.
    pub group: Vec<Instruction>,
}

/// Instructions on each side of a short site that may join its group.
const MAX_NEIGHBOURS: usize = 3;
/// The smallest redirect: `jmp rel32`.
const JUMP_BYTES: usize = 5;

/// Decodes only inside function ranges, so data bytes are never mistaken for code.
/// A module without `.eh_frame` falls back to its executable segments.
pub fn find_sites(image: &[u8], info: &ElfInfo) -> Result<Vec<Site>, TranslateError> {
    let mut sites: BTreeMap<u64, Site> = BTreeMap::new();
    for_each_range(image, info, &mut |instructions| {
        let targets = branch_targets(&instructions);
        for (index, instruction) in instructions.iter().enumerate() {
            if !uses_fs_memory(instruction) {
                continue;
            }
            let group = if instruction.len() < JUMP_BYTES {
                choose_group(&instructions, index, &targets)
            } else {
                Vec::new()
            };
            sites.entry(instruction.ip()).or_insert(Site {
                rva: instruction.ip(),
                instruction: *instruction,
                group,
            });
        }
    })?;
    Ok(sites.into_values().collect())
}

/// How many raw `syscall` instructions the module's code holds. Linux system calls cannot run on Windows, so
/// each one is a place where the program would misbehave; the translator reports them.
pub fn count_syscalls(image: &[u8], info: &ElfInfo) -> Result<usize, TranslateError> {
    let mut count = 0;
    for_each_range(image, info, &mut |instructions| {
        count += instructions
            .iter()
            .filter(|instruction| instruction.code() == Code::Syscall)
            .count();
    })?;
    Ok(count)
}

fn for_each_range(
    image: &[u8],
    info: &ElfInfo,
    visit: &mut dyn FnMut(Vec<Instruction>),
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
        visit(decode(bytes, start as u64));
    }
    Ok(())
}

/// The addresses direct branches and calls in the range jump to: a group must not hide one of them.
fn branch_targets(instructions: &[Instruction]) -> HashSet<u64> {
    instructions
        .iter()
        .filter(|instruction| {
            matches!(
                instruction.flow_control(),
                FlowControl::UnconditionalBranch
                    | FlowControl::ConditionalBranch
                    | FlowControl::Call
            )
        })
        .map(Instruction::near_branch_target)
        .filter(|&target| target != 0)
        .collect()
}

/// An instruction that can run in the stub: straight-line, or a conditional branch (the stub re-encodes it
/// to reach its original target); never a system call, and never a loop or `jrcxz`, which have no long form.
fn is_movable(instruction: &Instruction) -> bool {
    let is_plain = instruction.flow_control() == FlowControl::Next
        && !matches!(instruction.code(), Code::Syscall | Code::Sysenter);
    let is_conditional_jump = instruction.flow_control() == FlowControl::ConditionalBranch
        && !matches!(
            instruction.mnemonic(),
            Mnemonic::Jrcxz
                | Mnemonic::Jecxz
                | Mnemonic::Jcxz
                | Mnemonic::Loop
                | Mnemonic::Loope
                | Mnemonic::Loopne
        );
    is_plain || is_conditional_jump
}

/// A neighbour that is itself an FS access the stub can retarget (not the canary, which is rewritten in place).
fn is_retargetable_fs(instruction: &Instruction) -> bool {
    let is_canary = instruction.memory_base() == Register::None
        && instruction.memory_index() == Register::None
        && instruction.memory_displacement64() == 0x28;
    uses_fs_memory(instruction)
        && !is_canary
        && (instruction.memory_base() == Register::None
            || instruction.memory_index() == Register::None)
}

/// The smallest run of instructions around `instructions[index]` that is long enough for a jump, contiguous,
/// movable, and has no known branch target inside it. Empty when there is none.
fn choose_group(
    instructions: &[Instruction],
    index: usize,
    targets: &HashSet<u64>,
) -> Vec<Instruction> {
    for total in 1..=2 * MAX_NEIGHBOURS {
        for before in 0..=total.min(MAX_NEIGHBOURS) {
            let after = total - before;
            if after > MAX_NEIGHBOURS || index < before || index + after >= instructions.len() {
                continue;
            }
            let run = &instructions[index - before..=index + after];
            if is_usable_group(run, targets) {
                return run.to_vec();
            }
        }
    }
    Vec::new()
}

fn is_usable_group(run: &[Instruction], targets: &HashSet<u64>) -> bool {
    let length: usize = run.iter().map(Instruction::len).sum();
    let is_contiguous = run.windows(2).all(|pair| pair[0].next_ip() == pair[1].ip());
    let hides_target = run
        .iter()
        .skip(1)
        .any(|instruction| targets.contains(&instruction.ip()));
    let fs_count = run
        .iter()
        .filter(|instruction| uses_fs_memory(instruction))
        .count();
    let is_fs_ok = run
        .iter()
        .filter(|instruction| uses_fs_memory(instruction))
        .all(|instruction| fs_count == 1 || is_retargetable_fs(instruction));
    length >= JUMP_BYTES && is_contiguous && !hides_target && is_fs_ok && run.iter().all(is_movable)
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

fn decode(bytes: &[u8], ip: u64) -> Vec<Instruction> {
    let mut instructions = Vec::new();
    let mut decoder = Decoder::with_ip(64, bytes, ip, DecoderOptions::NONE);
    while decoder.can_decode() {
        let position = decoder.position();
        let instruction = decoder.decode();
        if instruction.is_invalid() {
            if decoder.set_position(position + 1).is_err() {
                break;
            }
            decoder.set_ip(ip + position as u64 + 1);
            continue;
        }
        instructions.push(instruction);
    }
    instructions
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

    /// An image with `code` placed at a spot outside the real code, and a code range that covers only it.
    fn with_code(code: &[u8]) -> (Vec<u8>, ElfInfo, u64) {
        let mut info = analyze(TLS_BASIC).expect("parses");
        let mut image = image_of(TLS_BASIC, &info);
        let start = 0x300_0000u64;
        let at = (RVA_BIAS + start) as usize;
        image.resize(at + code.len() + 16, 0);
        image[at..at + code.len()].copy_from_slice(code);
        info.code_ranges = vec![CodeRange {
            start,
            len: code.len() as u64,
        }];
        (image, info, RVA_BIAS + start)
    }

    // mov %rdx,%rcx ; mov %fs:(%rax),%rbx ; ret
    const SHORT_SITE: [u8; 8] = [0x48, 0x89, 0xd1, 0x64, 0x48, 0x8b, 0x18, 0xc3];

    #[test]
    fn a_short_access_is_grouped_with_the_instruction_before_it() {
        let (image, info, start) = with_code(&SHORT_SITE);
        let sites = find_sites(&image, &info).expect("scans");
        assert_eq!(sites.len(), 1);
        assert_eq!(sites[0].rva, start + 3);
        let ips: Vec<u64> = sites[0].group.iter().map(Instruction::ip).collect();
        assert_eq!(
            ips,
            [start, start + 3],
            "the ret cannot move, so the mov before joins"
        );
    }

    #[test]
    fn a_group_never_hides_a_branch_target() {
        // je +3 (to the access) ; mov %rdx,%rcx ; mov %fs:(%rax),%rbx ; ret
        let code = [0x74, 0x03, 0x48, 0x89, 0xd1, 0x64, 0x48, 0x8b, 0x18, 0xc3];
        let (image, info, _) = with_code(&code);
        let sites = find_sites(&image, &info).expect("scans");
        assert_eq!(sites.len(), 1);
        assert!(
            sites[0].group.is_empty(),
            "the jump lands on the access, so nothing may swallow it"
        );
    }

    #[test]
    fn a_long_enough_access_needs_no_group() {
        // mov %fs:0xffffffffffffff90,%eax (8 bytes)
        let (image, info, _) = with_code(&[0x64, 0x8b, 0x04, 0x25, 0x90, 0xff, 0xff, 0xff]);
        let sites = find_sites(&image, &info).expect("scans");
        assert_eq!(sites.len(), 1);
        assert!(sites[0].group.is_empty());
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
