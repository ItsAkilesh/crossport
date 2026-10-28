// File: translator/src/analyze/header.rs
// Purpose: Validate the ELF header and extract PT_LOAD segments, rejecting what Crossport cannot translate
// Author: Akilesh
// Date: 2026-10-28

use object::elf;
use object::read::elf::{FileHeader, ProgramHeader};
use object::Endianness;

use super::address_map::add;
use super::{Header, Phdr};
use crate::elf_model::{LoadSegment, SegmentFlags, TlsSegment};
use crate::error::TranslateError;

const MAX_SEGMENTS: usize = 64;
/// Largest TLS template accepted for one module.
const MAX_TLS_BYTES: u64 = 16 * 1024 * 1024;
/// Upper bound for one module's mapped size, so a hostile `p_memsz` cannot exhaust memory.
pub const MAX_IMAGE_BYTES: u64 = 256 * 1024 * 1024;

pub(super) fn check_header(header: &Header, endian: Endianness) -> Result<(), TranslateError> {
    if header.e_machine(endian) != elf::EM_X86_64 {
        return Err(TranslateError::Unsupported(
            "only x86-64 ELF files are supported".into(),
        ));
    }
    if header.e_type(endian) != elf::ET_DYN {
        return Err(TranslateError::Unsupported(
            "only position-independent executables and shared objects (ET_DYN) are supported"
                .into(),
        ));
    }
    Ok(())
}

/// The `PT_TLS` template, if the module has thread-local variables.
pub(super) fn tls_segment(
    phdrs: &[Phdr],
    endian: Endianness,
) -> Result<Option<TlsSegment>, TranslateError> {
    let mut found = None;
    for ph in phdrs.iter().filter(|ph| ph.p_type(endian) == elf::PT_TLS) {
        let align = ph.p_align(endian).max(1);
        let segment = TlsSegment {
            vaddr: ph.p_vaddr(endian),
            file_size: ph.p_filesz(endian),
            mem_size: ph.p_memsz(endian),
            align,
        };
        let is_valid = align.is_power_of_two()
            && segment.file_size <= segment.mem_size
            && segment.mem_size <= MAX_TLS_BYTES
            && segment.vaddr.checked_add(segment.file_size).is_some();
        if !is_valid || found.replace(segment).is_some() {
            return Err(TranslateError::Elf(
                "invalid or duplicate PT_TLS segment".into(),
            ));
        }
    }
    Ok(found)
}

pub(super) fn load_segments(
    phdrs: &[Phdr],
    endian: Endianness,
) -> Result<Vec<LoadSegment>, TranslateError> {
    let mut segments: Vec<LoadSegment> = Vec::new();
    for ph in phdrs.iter().filter(|ph| ph.p_type(endian) == elf::PT_LOAD) {
        let segment = to_segment(ph, endian);
        validate_segment(&segment, segments.last())?;
        segments.push(segment);
        if segments.len() > MAX_SEGMENTS {
            return Err(TranslateError::LimitExceeded(format!(
                "more than {MAX_SEGMENTS} PT_LOAD segments"
            )));
        }
    }
    if segments.is_empty() {
        return Err(TranslateError::Elf("no PT_LOAD segments".into()));
    }
    Ok(segments)
}

fn to_segment(ph: &Phdr, endian: Endianness) -> LoadSegment {
    let flags = ph.p_flags(endian);
    LoadSegment {
        vaddr: ph.p_vaddr(endian),
        file_offset: ph.p_offset(endian),
        file_size: ph.p_filesz(endian),
        mem_size: ph.p_memsz(endian),
        flags: SegmentFlags {
            is_readable: flags.0 & elf::PF_R.0 != 0,
            is_writable: flags.0 & elf::PF_W.0 != 0,
            is_executable: flags.0 & elf::PF_X.0 != 0,
        },
    }
}

fn validate_segment(
    segment: &LoadSegment,
    previous: Option<&LoadSegment>,
) -> Result<(), TranslateError> {
    if segment.file_size > segment.mem_size {
        return Err(TranslateError::Elf(
            "segment file size exceeds its memory size".into(),
        ));
    }
    let end = add(segment.vaddr, segment.mem_size)?;
    add(segment.file_offset, segment.file_size)?;
    if end > MAX_IMAGE_BYTES {
        return Err(TranslateError::LimitExceeded(format!(
            "module image larger than {MAX_IMAGE_BYTES} bytes"
        )));
    }
    if let Some(previous) = previous {
        if segment.vaddr < previous.vaddr + previous.mem_size {
            return Err(TranslateError::Elf(
                "PT_LOAD segments overlap or are not in ascending order".into(),
            ));
        }
    }
    Ok(())
}
