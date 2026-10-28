// File: translator/src/stub.rs
// Purpose: Generate the PE entry stub and the start-info table the shim reads before running the ELF entry
// Author: Akilesh
// Date: 2026-10-28

/// "CPRT" read as a little-endian u32. Must match `shim/crossport_libc.c`.
pub const START_INFO_MAGIC: u32 = 0x5452_5043;
pub const START_INFO_VERSION: u32 = 1;

const STUB_LEN: usize = 13;
const INFO_OFFSET: usize = 16;
/// `lea rcx, [rip + disp32]`
const LEA_RCX_RIP: [u8; 3] = [0x48, 0x8D, 0x0D];
/// `jmp qword ptr [rip + disp32]`
const JMP_RIP_INDIRECT: [u8; 2] = [0xFF, 0x25];
const LEA_LEN: u32 = 7;

/// Everything the shim needs to bring the translated program up. All
/// addresses are RVAs; zero means "absent".
pub struct StartInfo {
    pub entry_rva: u32,
    pub init_rva: u32,
    pub init_array_rva: u32,
    pub init_array_count: u32,
    pub fini_rva: u32,
    pub fini_array_rva: u32,
    pub fini_array_count: u32,
    /// `(got_rva, iat_rva)`: the shim copies each IAT entry into its GOT slot.
    pub got_pairs: Vec<(u32, u32)>,
}

/// Builds the section contents: stub code at offset 0, the info table at 16.
/// The stub passes the info table to `crossport_start` in `rcx` and jumps
/// through that function's IAT slot, so the Windows entry convention is kept.
pub fn build(section_rva: u32, info: &StartInfo, start_iat_rva: u32) -> Vec<u8> {
    let info_rva = section_rva + INFO_OFFSET as u32;
    let mut out = Vec::new();
    out.extend_from_slice(&LEA_RCX_RIP);
    out.extend_from_slice(&info_rva.wrapping_sub(section_rva + LEA_LEN).to_le_bytes());
    out.extend_from_slice(&JMP_RIP_INDIRECT);
    let jmp_end = section_rva + STUB_LEN as u32;
    out.extend_from_slice(&start_iat_rva.wrapping_sub(jmp_end).to_le_bytes());
    out.resize(INFO_OFFSET, 0xCC);
    encode_info(&mut out, info);
    out
}

fn encode_info(out: &mut Vec<u8>, info: &StartInfo) {
    let header = [
        START_INFO_MAGIC,
        START_INFO_VERSION,
        info.entry_rva,
        info.init_rva,
        info.init_array_rva,
        info.init_array_count,
        info.fini_rva,
        info.fini_array_rva,
        info.fini_array_count,
        info.got_pairs.len() as u32,
    ];
    for value in header {
        out.extend_from_slice(&value.to_le_bytes());
    }
    for &(got_rva, iat_rva) in &info.got_pairs {
        out.extend_from_slice(&got_rva.to_le_bytes());
        out.extend_from_slice(&iat_rva.to_le_bytes());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn info() -> StartInfo {
        StartInfo {
            entry_rva: 0x2060,
            init_rva: 0x2000,
            init_array_rva: 0x4de8,
            init_array_count: 1,
            fini_rva: 0x214c,
            fini_array_rva: 0x4df0,
            fini_array_count: 1,
            got_pairs: vec![(0x4fe0, 0x7060)],
        }
    }

    fn u32_at(bytes: &[u8], at: usize) -> u32 {
        u32::from_le_bytes(bytes[at..at + 4].try_into().expect("4 bytes"))
    }

    #[test]
    fn stub_loads_info_address_and_jumps_through_the_iat() {
        let bytes = build(0x8000, &info(), 0x7078);
        assert_eq!(&bytes[0..3], &LEA_RCX_RIP);
        assert_eq!(0x8000 + 7 + u32_at(&bytes, 3), 0x8000 + INFO_OFFSET as u32);
        assert_eq!(&bytes[7..9], &JMP_RIP_INDIRECT);
        assert_eq!(0x800Du32.wrapping_add(u32_at(&bytes, 9)), 0x7078);
    }

    #[test]
    fn info_table_starts_with_magic_and_lists_pairs() {
        let bytes = build(0x8000, &info(), 0x7078);
        assert_eq!(u32_at(&bytes, INFO_OFFSET), START_INFO_MAGIC);
        assert_eq!(u32_at(&bytes, INFO_OFFSET + 8), 0x2060);
        assert_eq!(u32_at(&bytes, INFO_OFFSET + 36), 1, "pair count");
        assert_eq!(u32_at(&bytes, INFO_OFFSET + 40), 0x4fe0);
        assert_eq!(u32_at(&bytes, INFO_OFFSET + 44), 0x7060);
    }
}
