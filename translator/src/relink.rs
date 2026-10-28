// File: translator/src/relink.rs
// Purpose: Relink an analyzed ELF executable into a PE image bound to the Crossport shim DLL
// Author: Akilesh
// Date: 2026-10-28

use crate::base_reloc;
use crate::elf_model::{ElfInfo, LoadSegment, RelocKind};
use crate::error::TranslateError;
use crate::imports::{self, ImportTables};
use crate::pe_model::{
    align_down, align_up, DataDirectory, PeImage, PeSection, IMAGE_BASE, PAGE_SIZE, RVA_BIAS,
    SCN_CODE, SCN_DISCARDABLE, SCN_EXECUTE, SCN_INITIALIZED_DATA, SCN_READ, SCN_WRITE,
};
use crate::stub::{self, StartInfo};

pub const SHIM_DLL: &str = "crossport_libc.dll";
pub const START_SYMBOL: &str = "crossport_start";
const SUPPORTED_LIBRARY: &str = "libc.so.6";

/// A PT_LOAD segment placed in the PE address space.
struct Placement {
    rva: u64,
    file_len: u64,
    mem_len: u64,
    flags: u32,
}

struct Patched {
    base_reloc_slots: Vec<u32>,
    got_slots: Vec<(String, u32)>,
}

pub fn relink(info: &ElfInfo, data: &[u8]) -> Result<PeImage, TranslateError> {
    check_supported(info)?;
    let placements = place_segments(&info.segments)?;
    let end_rva = placements.iter().map(|p| p.rva + align_up(p.mem_len, PAGE_SIZE)).max().unwrap_or(RVA_BIAS);
    let mut image = vec![0u8; end_rva as usize];
    copy_segments(&mut image, info, data)?;
    let patched = patch_relocations(&mut image, info)?;

    let mut sections = elf_sections(&placements, &image);
    let names = shim_import_names(info);
    let idata_rva = end_rva as u32;
    let idata = imports::build(SHIM_DLL, &names, idata_rva);
    let cport_rva = idata_rva + align_up(idata.data.len() as u64, PAGE_SIZE) as u32;
    let start_info = start_info(info, &patched, &idata)?;
    let start_iat = idata.iat_rva(START_SYMBOL).ok_or_else(missing_start)?;
    let cport = stub::build(cport_rva, &start_info, start_iat);
    let reloc_rva = cport_rva + align_up(cport.len() as u64, PAGE_SIZE) as u32;
    let reloc = base_reloc::build(&patched.base_reloc_slots);

    let directory = DataDirectory { rva: reloc_rva, size: reloc.len() as u32 };
    sections.push(data_section(".idata", idata_rva, idata.data.clone(), SCN_READ | SCN_WRITE));
    sections.push(data_section(".cport", cport_rva, cport, SCN_READ | SCN_EXECUTE | SCN_CODE));
    sections.push(data_section(".reloc", reloc_rva, reloc, SCN_READ | SCN_DISCARDABLE));
    Ok(PeImage {
        entry_rva: cport_rva,
        sections,
        import_directory: idata.directory,
        base_reloc_directory: directory,
    })
}

fn missing_start() -> TranslateError {
    TranslateError::Elf("internal: start symbol missing from import list".into())
}

fn data_section(name: &str, rva: u32, data: Vec<u8>, extra: u32) -> PeSection {
    PeSection {
        name: name.to_string(),
        rva,
        virtual_size: data.len() as u32,
        data,
        characteristics: SCN_INITIALIZED_DATA | extra,
    }
}

fn check_supported(info: &ElfInfo) -> Result<(), TranslateError> {
    match info.needed.iter().find(|lib| lib.as_str() != SUPPORTED_LIBRARY) {
        Some(lib) => Err(TranslateError::Unsupported(format!(
            "dependency {lib} (only {SUPPORTED_LIBRARY} is shimmed; bundled libraries arrive in Milestone 2)"
        ))),
        None => Ok(()),
    }
}

fn place_segments(segments: &[LoadSegment]) -> Result<Vec<Placement>, TranslateError> {
    let mut placements: Vec<Placement> = Vec::new();
    for segment in segments {
        let start = align_down(segment.vaddr, PAGE_SIZE);
        let mem_end = segment.vaddr + segment.mem_size;
        let file_end = segment.vaddr + segment.file_size;
        if let Some(previous) = placements.last() {
            if RVA_BIAS + start < previous.rva + align_up(previous.mem_len, PAGE_SIZE) {
                return Err(TranslateError::Unsupported("two segments share a page".into()));
            }
        }
        placements.push(Placement {
            rva: RVA_BIAS + start,
            file_len: file_end - start,
            mem_len: mem_end - start,
            flags: characteristics(segment),
        });
    }
    Ok(placements)
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

fn copy_segments(
    image: &mut [u8],
    info: &ElfInfo,
    data: &[u8],
) -> Result<(), TranslateError> {
    for segment in &info.segments {
        let source = data
            .get(segment.file_offset as usize..(segment.file_offset + segment.file_size) as usize)
            .ok_or_else(|| TranslateError::Elf("segment extends past the end of the file".into()))?;
        let at = (RVA_BIAS + segment.vaddr) as usize;
        image[at..at + source.len()].copy_from_slice(source);
    }
    Ok(())
}

fn elf_sections(placements: &[Placement], image: &[u8]) -> Vec<PeSection> {
    placements
        .iter()
        .enumerate()
        .map(|(index, p)| {
            let raw_len = align_up(p.file_len, PAGE_SIZE) as usize;
            let start = p.rva as usize;
            PeSection {
                name: format!(".elf{index}"),
                rva: p.rva as u32,
                virtual_size: p.mem_len as u32,
                data: image[start..start + raw_len].to_vec(),
                characteristics: p.flags,
            }
        })
        .collect()
}

fn write_u64(image: &mut [u8], rva: u64, value: u64) -> Result<(), TranslateError> {
    let range = rva as usize..rva as usize + 8;
    let slot = image
        .get_mut(range)
        .ok_or_else(|| TranslateError::Elf(format!("relocation target {rva:#x} is outside the image")))?;
    slot.copy_from_slice(&value.to_le_bytes());
    Ok(())
}

/// Applies every relocation. `RELATIVE` slots become absolute pointers into
/// the preferred image base plus a PE base relocation. Weak imports are
/// resolved to null. Other imports are recorded so the shim can copy the
/// loader-filled IAT entry into the GOT slot before the program starts.
fn patch_relocations(image: &mut [u8], info: &ElfInfo) -> Result<Patched, TranslateError> {
    let mut patched = Patched { base_reloc_slots: Vec::new(), got_slots: Vec::new() };
    for relocation in &info.relocations {
        let slot = RVA_BIAS + relocation.offset;
        match &relocation.kind {
            RelocKind::Relative { addend } => {
                let value = (IMAGE_BASE + RVA_BIAS).wrapping_add(*addend as u64);
                write_u64(image, slot, value)?;
                patched.base_reloc_slots.push(slot as u32);
            }
            RelocKind::Import { symbol } => {
                let import = &info.imports[*symbol];
                if import.is_weak {
                    write_u64(image, slot, 0)?;
                } else {
                    patched.got_slots.push((import.name.clone(), slot as u32));
                }
            }
        }
    }
    Ok(patched)
}

/// Names the shim must export: each strongly imported symbol, then the start function.
fn shim_import_names(info: &ElfInfo) -> Vec<String> {
    let mut names: Vec<String> = Vec::new();
    for import in info.imports.iter().filter(|i| !i.is_weak) {
        if !names.contains(&import.name) {
            names.push(import.name.clone());
        }
    }
    names.push(START_SYMBOL.to_string());
    names
}

fn start_info(
    info: &ElfInfo,
    patched: &Patched,
    idata: &ImportTables,
) -> Result<StartInfo, TranslateError> {
    let rva = |vaddr: u64| (RVA_BIAS + vaddr) as u32;
    let mut got_pairs = Vec::new();
    for (name, got_rva) in &patched.got_slots {
        let iat_rva = idata.iat_rva(name).ok_or_else(missing_start)?;
        got_pairs.push((*got_rva, iat_rva));
    }
    Ok(StartInfo {
        entry_rva: rva(info.entry),
        init_rva: info.init.map_or(0, rva),
        init_array_rva: info.init_array.map_or(0, |a| rva(a.vaddr)),
        init_array_count: info.init_array.map_or(0, |a| a.count as u32),
        fini_rva: info.fini.map_or(0, rva),
        fini_array_rva: info.fini_array.map_or(0, |a| rva(a.vaddr)),
        fini_array_count: info.fini_array.map_or(0, |a| a.count as u32),
        got_pairs,
    })
}
