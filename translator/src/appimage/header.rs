// File: translator/src/appimage/header.rs
// Purpose: Recognise an AppImage (type 2) and find where its SquashFS payload begins
// Author: Akilesh
// Date: 2026-10-28

use crate::error::TranslateError;

const ELF_MAGIC: [u8; 4] = [0x7f, b'E', b'L', b'F'];
/// AppImage marks itself in the ELF header's padding: "AI" followed by the format version.
const APPIMAGE_MAGIC_OFFSET: usize = 8;
const APPIMAGE_MAGIC: [u8; 3] = [b'A', b'I', 2];
const ELF64_HEADER_BYTES: usize = 64;
const E_SHOFF: usize = 0x28;
const E_SHENTSIZE: usize = 0x3A;
const E_SHNUM: usize = 0x3C;
const SQUASHFS_MAGIC: [u8; 4] = *b"hsqs";
/// The first bytes of the file every check needs.
pub const PREFIX_BYTES: usize = ELF64_HEADER_BYTES;

fn not_appimage(reason: &str) -> TranslateError {
    TranslateError::Unsupported(format!("not a type 2 AppImage: {reason}"))
}

fn u16_at(bytes: &[u8], at: usize) -> u64 {
    u64::from(u16::from_le_bytes([bytes[at], bytes[at + 1]]))
}

fn u64_at(bytes: &[u8], at: usize) -> u64 {
    let mut raw = [0u8; 8];
    raw.copy_from_slice(&bytes[at..at + 8]);
    u64::from_le_bytes(raw)
}

/// The SquashFS image sits directly after the ELF runtime, whose end is the end of its section
/// header table. `prefix` is the start of the file and `file_len` its length; `magic_at` reads
/// the four bytes at an offset so the caller never has to load the whole file.
pub fn squashfs_offset(
    prefix: &[u8],
    file_len: u64,
    magic_at: impl FnOnce(u64) -> Option<[u8; 4]>,
) -> Result<u64, TranslateError> {
    if prefix.len() < ELF64_HEADER_BYTES || prefix[..4] != ELF_MAGIC {
        return Err(not_appimage("it is not an ELF file"));
    }
    if prefix[APPIMAGE_MAGIC_OFFSET..APPIMAGE_MAGIC_OFFSET + 3] != APPIMAGE_MAGIC {
        return Err(not_appimage(
            "the AI\\x02 marker is missing (type 1 AppImages are not supported)",
        ));
    }
    let section_table = u64_at(prefix, E_SHOFF);
    let end = section_table
        .checked_add(u64_at_u16_product(prefix))
        .ok_or_else(|| not_appimage("section table overflows"))?;
    if end >= file_len {
        return Err(not_appimage(
            "the SquashFS payload would start beyond the end of the file",
        ));
    }
    match magic_at(end) {
        Some(magic) if magic == SQUASHFS_MAGIC => Ok(end),
        _ => Err(not_appimage("no SquashFS magic after the runtime")),
    }
}

fn u64_at_u16_product(prefix: &[u8]) -> u64 {
    u16_at(prefix, E_SHENTSIZE) * u16_at(prefix, E_SHNUM)
}

/// A minimal AppImage-shaped header whose runtime ends at `runtime_end` (no section headers).
#[cfg(test)]
pub(crate) fn test_header(runtime_end: u64) -> Vec<u8> {
    let mut bytes = vec![0u8; ELF64_HEADER_BYTES];
    bytes[..4].copy_from_slice(&ELF_MAGIC);
    bytes[APPIMAGE_MAGIC_OFFSET..APPIMAGE_MAGIC_OFFSET + 3].copy_from_slice(&APPIMAGE_MAGIC);
    bytes[E_SHOFF..E_SHOFF + 8].copy_from_slice(&runtime_end.to_le_bytes());
    bytes
}

#[cfg(test)]
mod tests {
    use super::*;

    fn header(runtime_end: u64) -> Vec<u8> {
        test_header(runtime_end)
    }

    fn found(_: u64) -> Option<[u8; 4]> {
        Some(SQUASHFS_MAGIC)
    }

    #[test]
    fn the_payload_starts_where_the_section_table_ends() {
        let mut bytes = header(1000);
        bytes[E_SHENTSIZE..E_SHENTSIZE + 2].copy_from_slice(&64u16.to_le_bytes());
        bytes[E_SHNUM..E_SHNUM + 2].copy_from_slice(&3u16.to_le_bytes());
        assert_eq!(
            squashfs_offset(&bytes, 5000, found).expect("valid"),
            1000 + 64 * 3
        );
    }

    #[test]
    fn a_plain_elf_or_a_type_1_image_is_rejected() {
        let mut plain = header(100);
        plain[APPIMAGE_MAGIC_OFFSET..APPIMAGE_MAGIC_OFFSET + 3].fill(0);
        assert!(squashfs_offset(&plain, 5000, found).is_err());
        assert!(squashfs_offset(
            b"not elf at all, but long enough to pass the length check........",
            5000,
            found
        )
        .is_err());
        assert!(squashfs_offset(&[0x7f, b'E'], 5000, found).is_err());
    }

    #[test]
    fn a_missing_squashfs_magic_or_an_out_of_range_offset_is_rejected() {
        let bytes = header(1000);
        assert!(squashfs_offset(&bytes, 5000, |_| Some(*b"nope")).is_err());
        assert!(squashfs_offset(&bytes, 5000, |_| None).is_err());
        assert!(squashfs_offset(&bytes, 1000, found).is_err());
        let mut huge = header(u64::MAX);
        huge[E_SHENTSIZE..E_SHENTSIZE + 2].copy_from_slice(&64u16.to_le_bytes());
        huge[E_SHNUM..E_SHNUM + 2].copy_from_slice(&2u16.to_le_bytes());
        assert!(squashfs_offset(&huge, 5000, found).is_err());
    }
}
