// File: translator/src/startup.rs
// Purpose: Encode the per-module start-info table the shim reads, and the executable's 13-byte entry stub
// Author: Akilesh
// Date: 2026-10-29

use crate::error::TranslateError;
use crate::shim_abi::{
    INFO_HEADER_BYTES, INFO_MAGIC, INFO_VERSION, MAX_LISTED_MODULES, RECORD_BYTES,
};

const STUB_LEN: usize = 13;
const EXECUTABLE_INFO_OFFSET: usize = 16;
const PADDING_BYTE: u8 = 0xCC;
/// `lea rcx, [rip + disp32]`
const LEA_RCX_RIP: [u8; 3] = [0x48, 0x8D, 0x0D];
/// `jmp qword ptr [rip + disp32]`
const JMP_RIP_INDIRECT: [u8; 2] = [0xFF, 0x25];
const LEA_LEN: u32 = 7;

/// `*slot = *source + addend`, where `source` is an IAT entry (library and shim
/// bindings) or an RVA inside the executable (executable bindings).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Binding {
    pub slot_rva: u32,
    pub source_rva: u32,
    pub addend: i64,
}

/// `memcpy(executable + dest_rva, *iat_rva, size)`: an `R_X86_64_COPY` performed at startup.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CopyRecord {
    pub dest_rva: u32,
    pub iat_rva: u32,
    pub size: u64,
}

/// Everything the shim needs to bring one module up. All addresses are RVAs; zero means absent.
#[derive(Debug, Clone, Default)]
pub struct ModuleInfo {
    pub entry_rva: u32,
    pub init_rva: u32,
    pub init_array_rva: u32,
    pub init_array_count: u32,
    pub fini_rva: u32,
    pub fini_array_rva: u32,
    pub fini_array_count: u32,
    pub iat_bindings: Vec<Binding>,
    pub exe_bindings: Vec<Binding>,
    pub copies: Vec<CopyRecord>,
    /// DLL file names the executable's startup must load, dependencies first.
    pub dependencies: Vec<String>,
    pub tls: TlsInfo,
    /// RVA of the module's `.eh_frame_hdr`, which the shim reports through `dl_iterate_phdr`.
    pub eh_frame_hdr_rva: u32,
}

/// Thread-local storage data for one module. All zero when the module has none.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct TlsInfo {
    /// RVA of the initial-data template, `file_size` bytes of it.
    pub init_rva: u32,
    pub file_size: u32,
    pub mem_size: u32,
    pub align: u32,
    /// Start of the module's block relative to the thread pointer (negative).
    pub offset: i32,
    /// RVA of the module's stack-canary cell, or zero.
    pub canary_cell_rva: u32,
    /// Executable only: bytes of static TLS below the thread pointer, over all modules.
    pub total_size: u32,
    /// Executable only: alignment of the thread pointer.
    pub max_align: u32,
    /// RVAs of 4-byte displacements the shim patches with the Windows TLS slot offset.
    pub slot_fixups: Vec<u32>,
    /// The stub section: made executable (and read-only) by the shim once patched.
    pub stubs_rva: u32,
    pub stubs_size: u32,
}

pub struct StartupSection {
    pub data: Vec<u8>,
    /// Offset of the info table inside `data`.
    pub info_offset: u32,
}

/// Stub at offset 0 (passes the info table to `crossport_start` in `rcx`, then
/// jumps through that function's IAT slot, so the Windows entry convention is
/// kept) and the info table at offset 16.
pub fn build_executable(
    section_rva: u32,
    info: &ModuleInfo,
    start_iat_rva: u32,
) -> Result<StartupSection, TranslateError> {
    let info_rva = section_rva + EXECUTABLE_INFO_OFFSET as u32;
    let mut data = Vec::new();
    data.extend_from_slice(&LEA_RCX_RIP);
    data.extend_from_slice(&info_rva.wrapping_sub(section_rva + LEA_LEN).to_le_bytes());
    data.extend_from_slice(&JMP_RIP_INDIRECT);
    let jump_end = section_rva + STUB_LEN as u32;
    data.extend_from_slice(&start_iat_rva.wrapping_sub(jump_end).to_le_bytes());
    data.resize(EXECUTABLE_INFO_OFFSET, PADDING_BYTE);
    encode_info(&mut data, info, info_rva)?;
    Ok(StartupSection {
        data,
        info_offset: EXECUTABLE_INFO_OFFSET as u32,
    })
}

/// Libraries have no stub; the table is at offset 0 and exported as `__crossport_info`.
pub fn build_library(
    section_rva: u32,
    info: &ModuleInfo,
) -> Result<StartupSection, TranslateError> {
    let mut data = Vec::new();
    encode_info(&mut data, info, section_rva)?;
    Ok(StartupSection {
        data,
        info_offset: 0,
    })
}

fn push_u32(out: &mut Vec<u8>, value: u32) {
    out.extend_from_slice(&value.to_le_bytes());
}

fn push_record(out: &mut Vec<u8>, first: u32, second: u32, value: i64) {
    push_u32(out, first);
    push_u32(out, second);
    out.extend_from_slice(&value.to_le_bytes());
}

/// Layout: 26-field header, iat bindings, executable bindings, copies, TLS slot
/// fixups, the dependency name RVA table, then the dependency name strings.
fn encode_info(out: &mut Vec<u8>, info: &ModuleInfo, info_rva: u32) -> Result<(), TranslateError> {
    if info.dependencies.len() > MAX_LISTED_MODULES {
        return Err(TranslateError::LimitExceeded(format!(
            "more than {MAX_LISTED_MODULES} dependency modules"
        )));
    }
    let record_count = info.iat_bindings.len() + info.exe_bindings.len() + info.copies.len();
    let records_bytes = RECORD_BYTES * record_count + 4 * info.tls.slot_fixups.len();
    let table_rva = info_rva + (INFO_HEADER_BYTES + records_bytes) as u32;
    let tls = &info.tls;
    let header = [
        INFO_MAGIC,
        INFO_VERSION,
        info.entry_rva,
        info.init_rva,
        info.init_array_rva,
        info.init_array_count,
        info.fini_rva,
        info.fini_array_rva,
        info.fini_array_count,
        info.iat_bindings.len() as u32,
        info.exe_bindings.len() as u32,
        info.copies.len() as u32,
        info.dependencies.len() as u32,
        if info.dependencies.is_empty() {
            0
        } else {
            table_rva
        },
        tls.init_rva,
        tls.file_size,
        tls.mem_size,
        tls.align,
        tls.offset as u32,
        tls.canary_cell_rva,
        tls.slot_fixups.len() as u32,
        tls.total_size,
        tls.max_align,
        tls.stubs_rva,
        tls.stubs_size,
        info.eh_frame_hdr_rva,
    ];
    for field in header {
        push_u32(out, field);
    }
    for binding in info.iat_bindings.iter().chain(&info.exe_bindings) {
        push_record(out, binding.slot_rva, binding.source_rva, binding.addend);
    }
    for copy in &info.copies {
        push_record(out, copy.dest_rva, copy.iat_rva, copy.size as i64);
    }
    for fixup in &tls.slot_fixups {
        push_u32(out, *fixup);
    }
    encode_dependencies(out, &info.dependencies, table_rva);
    Ok(())
}

fn encode_dependencies(out: &mut Vec<u8>, names: &[String], table_rva: u32) {
    let mut string_rva = table_rva + (names.len() * 4) as u32;
    for name in names {
        push_u32(out, string_rva);
        string_rva += name.len() as u32 + 1;
    }
    for name in names {
        out.extend_from_slice(name.as_bytes());
        out.push(0);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn u32_at(bytes: &[u8], at: usize) -> u32 {
        u32::from_le_bytes(bytes[at..at + 4].try_into().expect("4 bytes"))
    }

    fn info() -> ModuleInfo {
        ModuleInfo {
            entry_rva: 0x2060,
            init_rva: 0x2000,
            init_array_rva: 0x4de8,
            init_array_count: 1,
            fini_rva: 0x214c,
            fini_array_rva: 0x4df0,
            fini_array_count: 1,
            iat_bindings: vec![Binding {
                slot_rva: 0x4fe0,
                source_rva: 0x7060,
                addend: 0,
            }],
            exe_bindings: vec![Binding {
                slot_rva: 0x4fe8,
                source_rva: 0x5010,
                addend: 8,
            }],
            copies: vec![CopyRecord {
                dest_rva: 0x5010,
                iat_rva: 0x7068,
                size: 4,
            }],
            dependencies: vec![
                "libmath.so.1.dll".to_string(),
                "libgreet.so.1.dll".to_string(),
            ],
            tls: TlsInfo::default(),
            eh_frame_hdr_rva: 0x2400,
        }
    }

    #[test]
    fn stub_loads_info_address_and_jumps_through_the_iat() {
        let section = build_executable(0x8000, &info(), 0x7078).expect("encodes");
        let bytes = &section.data;
        assert_eq!(&bytes[0..3], &LEA_RCX_RIP);
        assert_eq!(
            0x8000u32.wrapping_add(7).wrapping_add(u32_at(bytes, 3)),
            0x8010
        );
        assert_eq!(&bytes[7..9], &JMP_RIP_INDIRECT);
        assert_eq!(0x800Du32.wrapping_add(u32_at(bytes, 9)), 0x7078);
        assert_eq!(section.info_offset, 16);
    }

    #[test]
    fn header_counts_match_the_records_that_follow() {
        let section = build_library(0x8000, &info()).expect("encodes");
        let bytes = &section.data;
        assert_eq!(u32_at(bytes, 0), INFO_MAGIC);
        assert_eq!(u32_at(bytes, 4), INFO_VERSION);
        assert_eq!(u32_at(bytes, 36), 1, "iat bindings");
        assert_eq!(u32_at(bytes, 40), 1, "executable bindings");
        assert_eq!(u32_at(bytes, 44), 1, "copies");
        assert_eq!(u32_at(bytes, 48), 2, "dependencies");
        assert_eq!(
            u32_at(bytes, INFO_HEADER_BYTES - 4),
            0x2400,
            "the .eh_frame_hdr address is the last header field"
        );
        assert_eq!(u32_at(bytes, INFO_HEADER_BYTES), 0x4fe0);
        assert_eq!(
            i64::from_le_bytes(
                bytes[INFO_HEADER_BYTES + 24..INFO_HEADER_BYTES + 32]
                    .try_into()
                    .expect("8")
            ),
            8
        );
    }

    #[test]
    fn dependency_table_points_at_the_names_in_order() {
        let section = build_library(0x8000, &info()).expect("encodes");
        let bytes = &section.data;
        let table_rva = u32_at(bytes, 52);
        assert_eq!(
            table_rva,
            0x8000 + (INFO_HEADER_BYTES + 3 * RECORD_BYTES) as u32
        );
        let first = (u32_at(bytes, (table_rva - 0x8000) as usize) - 0x8000) as usize;
        assert_eq!(&bytes[first..first + 17], b"libmath.so.1.dll\0");
        let second = (u32_at(bytes, (table_rva - 0x8000) as usize + 4) - 0x8000) as usize;
        assert_eq!(&bytes[second..], b"libgreet.so.1.dll\0");
    }

    #[test]
    fn a_module_without_dependencies_has_a_null_table() {
        let section = build_library(0x8000, &ModuleInfo::default()).expect("encodes");
        assert_eq!(u32_at(&section.data, 52), 0);
        assert_eq!(section.data.len(), INFO_HEADER_BYTES);
    }

    #[test]
    fn too_many_dependencies_are_rejected() {
        let many = ModuleInfo {
            dependencies: vec!["x.dll".to_string(); MAX_LISTED_MODULES + 1],
            ..Default::default()
        };
        assert!(matches!(
            build_library(0x8000, &many),
            Err(TranslateError::LimitExceeded(_))
        ));
    }
}
