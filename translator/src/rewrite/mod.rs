// File: translator/src/rewrite/mod.rs
// Purpose: Rewrite FS-relative instructions, which Windows cannot run, into canary reads and thread-pointer stubs
// Author: Akilesh
// Date: 2026-11-05

mod scan;
mod stub;

use iced_x86::{Encoder, Instruction, Register};

use crate::error::TranslateError;
use crate::pe_model::{align_up, RVA_BIAS};
pub use scan::Site;

const CANARY_DISPLACEMENT: u64 = 0x28;
const STUB_ALIGNMENT: u64 = 16;
const JMP_REL32: u8 = 0xE9;
const JMP_LEN: usize = 5;
const NOP: u8 = 0x90;
const INT3: u8 = 0xCC;

pub use scan::{count_syscalls, find_sites};

/// How many sites of each kind were rewritten.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct RewriteStats {
    /// `%fs:0x28` reads, redirected in place to a per-module canary cell.
    pub canary_sites: usize,
    /// Every other FS-relative access, redirected to a stub with a 5-byte jump.
    pub stub_sites: usize,
    /// Raw `syscall` instructions, which are not translated (reported, not rewritten).
    pub syscall_sites: usize,
}

pub struct Rewrite {
    /// Contents of the stub section.
    pub stubs: Vec<u8>,
    /// Offsets inside `stubs` of the 4-byte displacements the shim patches with the TLS slot.
    pub slot_fixups: Vec<u32>,
    pub stats: RewriteStats,
}

fn is_canary(site: &Site) -> bool {
    let instruction = &site.instruction;
    instruction.memory_base() == Register::None
        && instruction.memory_index() == Register::None
        && instruction.memory_displacement64() == CANARY_DISPLACEMENT
}

/// Number of canary sites, so the caller knows whether a canary cell is needed.
pub fn count_canary_sites(sites: &[Site]) -> usize {
    sites.iter().filter(|site| is_canary(site)).count()
}

/// Patches `image` for every site. `canary_cell_rva` is required when canary
/// sites exist; `stubs_rva` is where the returned stub section will be mapped.
pub fn apply(
    image: &mut [u8],
    sites: &[Site],
    canary_cell_rva: Option<u32>,
    stubs_rva: u32,
) -> Result<Rewrite, TranslateError> {
    let mut rewrite = Rewrite {
        stubs: Vec::new(),
        slot_fixups: Vec::new(),
        stats: RewriteStats::default(),
    };
    let mut covered_until = 0;
    for site in sites {
        if site.rva < covered_until {
            continue; // already moved into the stub of a neighbouring site
        }
        if is_canary(site) {
            let cell = canary_cell_rva.ok_or_else(|| {
                TranslateError::Elf("internal: canary site without a canary cell".into())
            })?;
            patch_canary(image, site, cell)?;
            rewrite.stats.canary_sites += 1;
        } else {
            let (fs_accesses, end) =
                patch_with_stub(image, site, covered_until, stubs_rva, &mut rewrite)?;
            rewrite.stats.stub_sites += fs_accesses;
            covered_until = end;
        }
    }
    Ok(rewrite)
}

fn site_bytes<'a>(image: &'a mut [u8], site: &Site) -> Result<&'a mut [u8], TranslateError> {
    let start = site.rva as usize;
    image
        .get_mut(start..start + site.instruction.len())
        .ok_or_else(|| TranslateError::Elf("rewrite site lies outside the image".into()))
}

/// `mov reg, %fs:0x28` becomes `mov reg, [rip + canary_cell]`: shorter, so the
/// remainder is padded with NOPs and no trampoline is needed.
fn patch_canary(image: &mut [u8], site: &Site, cell_rva: u32) -> Result<(), TranslateError> {
    let mut modified = site.instruction;
    modified.set_segment_prefix(Register::None);
    modified.set_memory_base(Register::RIP);
    modified.set_memory_index(Register::None);
    modified.set_memory_displacement64(u64::from(cell_rva));
    modified.set_memory_displ_size(4);
    let mut encoder = Encoder::new(64);
    encoder.encode(&modified, site.rva).map_err(|error| {
        TranslateError::Unsupported(format!(
            "canary access at rva {:#x} cannot be re-encoded ({error})",
            site.rva
        ))
    })?;
    let encoded = encoder.take_buffer();
    let bytes = site_bytes(image, site)?;
    if encoded.len() > bytes.len() {
        return Err(TranslateError::Unsupported(format!(
            "canary access at rva {:#x} grew when re-encoded",
            site.rva
        )));
    }
    bytes[..encoded.len()].copy_from_slice(&encoded);
    bytes[encoded.len()..].fill(NOP);
    Ok(())
}

/// The instruction's bytes in hex, for error messages.
fn describe(image: &[u8], instruction: &Instruction) -> String {
    let start = instruction.ip() as usize;
    image
        .get(start..start + instruction.len())
        .unwrap_or_default()
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect::<Vec<_>>()
        .join(" ")
}

/// Redirects one FS access (and, when it is too short for a jump, the neighbours that join it) to a stub.
/// Returns how many FS accesses the stub covers and the address after the patched run.
fn patch_with_stub(
    image: &mut [u8],
    site: &Site,
    covered_until: u64,
    stubs_rva: u32,
    rewrite: &mut Rewrite,
) -> Result<(usize, u64), TranslateError> {
    let single = [site.instruction];
    let group: &[Instruction] = if site.instruction.len() >= JMP_LEN {
        &single
    } else if site.group.is_empty() {
        return Err(TranslateError::Unsupported(format!(
            "fs-relative instruction [{}] at rva {:#x} is {} bytes, too short to redirect, and no neighbours can move with it",
            describe(image, &site.instruction),
            site.rva,
            site.instruction.len()
        )));
    } else {
        &site.group
    };
    let start = group[0].ip();
    let end = group[group.len() - 1].next_ip();
    if start < covered_until {
        return Err(TranslateError::Unsupported(format!(
            "fs-relative instruction at rva {:#x}: its group overlaps the previous one",
            site.rva
        )));
    }
    let length = (end - start) as usize;
    let fs_accesses = group
        .iter()
        .filter(|instruction| instruction.segment_prefix() == Register::FS)
        .count();
    let stub_offset = align_up(rewrite.stubs.len() as u64, STUB_ALIGNMENT);
    let stub_rva = u64::from(stubs_rva) + stub_offset;
    let built = stub::build_group(group, site.rva, stub_rva, end)?;
    rewrite.stubs.resize(stub_offset as usize, INT3);
    rewrite
        .slot_fixups
        .push(stub_offset as u32 + built.fixup_offset);
    rewrite.stubs.extend_from_slice(&built.bytes);

    let displacement = i32::try_from(stub_rva as i64 - (start as i64 + JMP_LEN as i64))
        .map_err(|_| TranslateError::LimitExceeded("stub is out of jump range".into()))?;
    let bytes = image
        .get_mut(start as usize..start as usize + length)
        .ok_or_else(|| TranslateError::Elf("rewrite site lies outside the image".into()))?;
    bytes[0] = JMP_REL32;
    bytes[1..JMP_LEN].copy_from_slice(&displacement.to_le_bytes());
    bytes[JMP_LEN..].fill(INT3);
    Ok((fs_accesses, end))
}

/// Convenience for callers that only have a vaddr: the image index of a module address.
pub fn image_rva(vaddr: u64) -> u64 {
    RVA_BIAS + vaddr
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::analyze::analyze;
    use crate::elf_model::ElfInfo;
    use iced_x86::{Decoder, DecoderOptions};

    const TLS_BASIC: &[u8] = include_bytes!("../../tests/fixtures/tls_basic/tls_basic");
    const LIBTLS: &[u8] = include_bytes!("../../tests/fixtures/tls_basic/libtls.so.1");
    const CELL_RVA: u32 = 0x9000;
    const STUBS_RVA: u32 = 0xA000;

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

    fn rewritten(data: &[u8]) -> (Vec<u8>, Rewrite) {
        let info = analyze(data).expect("parses");
        let mut image = image_of(data, &info);
        let sites = find_sites(&image, &info).expect("scans");
        let result = apply(&mut image, &sites, Some(CELL_RVA), STUBS_RVA).expect("rewrites");
        (image, result)
    }

    fn patched_snippet(code: &[u8]) -> (Vec<u8>, Rewrite, u64) {
        let mut info = analyze(TLS_BASIC).expect("parses");
        let mut image = image_of(TLS_BASIC, &info);
        let start = 0x300_0000u64;
        let at = (RVA_BIAS + start) as usize;
        image.resize(at + code.len() + 16, 0);
        image[at..at + code.len()].copy_from_slice(code);
        info.code_ranges = vec![crate::elf_model::CodeRange {
            start,
            len: code.len() as u64,
        }];
        let sites = find_sites(&image, &info).expect("scans");
        let result = apply(&mut image, &sites, Some(CELL_RVA), STUBS_RVA).expect("rewrites");
        (image, result, RVA_BIAS + start)
    }

    #[test]
    fn a_short_access_and_its_neighbour_become_one_jump_into_one_stub() {
        // mov %rdx,%rcx ; mov %fs:(%rax),%rbx ; ret
        let (image, result, start) =
            patched_snippet(&[0x48, 0x89, 0xd1, 0x64, 0x48, 0x8b, 0x18, 0xc3]);
        let at = start as usize;
        assert_eq!(
            image[at], JMP_REL32,
            "the jump replaces the first instruction of the group"
        );
        assert!(image[at + 5..at + 7].iter().all(|&byte| byte == INT3));
        assert_eq!(image[at + 7], 0xc3, "the ret after the group is untouched");
        assert_eq!(result.stats.stub_sites, 1);
        assert_eq!(result.slot_fixups.len(), 1);
    }

    #[test]
    fn two_adjacent_short_accesses_share_a_stub() {
        // mov %fs:(%rax),%rbx twice, then ret
        let code = [0x64, 0x48, 0x8b, 0x18, 0x64, 0x48, 0x8b, 0x18, 0xc3];
        let (_, result, _) = patched_snippet(&code);
        assert_eq!(result.stats.stub_sites, 2, "both accesses are counted");
        assert_eq!(result.slot_fixups.len(), 1, "but there is one stub");
    }

    #[test]
    fn counts_match_the_known_instruction_mix() {
        let (_, result) = rewritten(TLS_BASIC);
        assert_eq!(
            result.stats.canary_sites, 4,
            "two functions, prologue and epilogue each"
        );
        assert_eq!(
            result.stats.stub_sites, 9,
            "local-exec, initial-exec and thread-pointer accesses"
        );
        assert_eq!(result.slot_fixups.len(), result.stats.stub_sites);
    }

    #[test]
    fn a_canary_read_becomes_a_rip_relative_load_of_the_cell() {
        let (image, _) = rewritten(TLS_BASIC);
        let at = (RVA_BIAS + 0x10ad) as usize;
        let instruction =
            Decoder::with_ip(64, &image[at..at + 9], at as u64, DecoderOptions::NONE).decode();
        assert!(instruction.is_ip_rel_memory_operand());
        assert_eq!(instruction.ip_rel_memory_address(), u64::from(CELL_RVA));
        assert_eq!(instruction.segment_prefix(), Register::None);
    }

    #[test]
    fn a_local_exec_access_becomes_a_jump_into_the_stub_area() {
        let (image, result) = rewritten(TLS_BASIC);
        let at = (RVA_BIAS + 0x10c0) as usize;
        assert_eq!(image[at], JMP_REL32);
        let displacement = i32::from_le_bytes(image[at + 1..at + 5].try_into().expect("4 bytes"));
        let target = (at as i64 + 5 + i64::from(displacement)) as u64;
        assert!(
            target >= u64::from(STUBS_RVA)
                && target < u64::from(STUBS_RVA) + result.stubs.len() as u64
        );
    }

    #[test]
    fn no_fs_prefixed_memory_instruction_survives_in_any_function() {
        for data in [TLS_BASIC, LIBTLS] {
            let info = analyze(data).expect("parses");
            let (image, _) = rewritten(data);
            assert!(find_sites(&image, &info).expect("rescans").is_empty());
        }
    }

    #[test]
    fn the_library_has_only_canary_sites() {
        let (_, result) = rewritten(LIBTLS);
        assert_eq!(result.stats.stub_sites, 0);
        assert_eq!(result.stats.canary_sites, 8);
    }

    #[test]
    fn a_canary_site_without_a_cell_is_an_error() {
        let info = analyze(LIBTLS).expect("parses");
        let mut image = image_of(LIBTLS, &info);
        let sites = find_sites(&image, &info).expect("scans");
        assert!(apply(&mut image, &sites, None, STUBS_RVA).is_err());
    }
}
