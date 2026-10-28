// File: translator/src/analyze/code_ranges.rs
// Purpose: Read function address ranges from .eh_frame so later passes decode real code, not data
// Author: Akilesh
// Date: 2026-10-28

use std::collections::HashMap;

use object::elf;
use object::read::elf::ProgramHeader;
use object::read::ReadRef;
use object::Endianness;

use super::address_map::{add, AddressMap};
use super::Phdr;
use crate::elf_model::CodeRange;
use crate::error::TranslateError;

const MAX_ENTRIES: usize = 1 << 20;
const EXTENDED_LENGTH_MARKER: u32 = 0xffff_ffff;
const FORMAT_MASK: u8 = 0x0f;
const APPLICATION_MASK: u8 = 0x70;
const APPLICATION_NONE: u8 = 0x00;
const APPLICATION_PCREL: u8 = 0x10;
const ENCODING_OMIT: u8 = 0xff;
const HEADER_VERSION: u8 = 1;

fn bad(message: &str) -> TranslateError {
    TranslateError::Elf(format!(".eh_frame: {message}"))
}

/// Cursor over a byte slice that remembers the virtual address of its first byte.
struct Reader<'a> {
    bytes: &'a [u8],
    position: usize,
    vaddr: u64,
}

impl<'a> Reader<'a> {
    fn new(bytes: &'a [u8], vaddr: u64) -> Self {
        Self {
            bytes,
            position: 0,
            vaddr,
        }
    }

    fn current_vaddr(&self) -> u64 {
        self.vaddr + self.position as u64
    }

    fn take(&mut self, count: usize) -> Result<&'a [u8], TranslateError> {
        let end = self
            .position
            .checked_add(count)
            .ok_or_else(|| bad("length overflow"))?;
        let slice = self
            .bytes
            .get(self.position..end)
            .ok_or_else(|| bad("truncated entry"))?;
        self.position = end;
        Ok(slice)
    }

    fn u8(&mut self) -> Result<u8, TranslateError> {
        Ok(self.take(1)?[0])
    }

    fn u32(&mut self) -> Result<u32, TranslateError> {
        let bytes: [u8; 4] = self.take(4)?.try_into().map_err(|_| bad("short read"))?;
        Ok(u32::from_le_bytes(bytes))
    }

    fn uleb(&mut self) -> Result<u64, TranslateError> {
        let mut result = 0u64;
        for shift in (0..64).step_by(7) {
            let byte = self.u8()?;
            result |= u64::from(byte & 0x7f) << shift;
            if byte & 0x80 == 0 {
                return Ok(result);
            }
        }
        Err(bad("unterminated LEB128"))
    }

    fn sleb(&mut self) -> Result<i64, TranslateError> {
        let mut result = 0i64;
        for shift in (0..64).step_by(7) {
            let byte = self.u8()?;
            result |= i64::from(byte & 0x7f) << shift;
            if byte & 0x80 == 0 {
                let sign_bit = 6 + shift;
                if sign_bit < 63 && byte & 0x40 != 0 {
                    result |= -1i64 << (shift + 7);
                }
                return Ok(result);
            }
        }
        Err(bad("unterminated LEB128"))
    }

    /// Reads a pointer in the DWARF exception-header encoding `encoding`.
    fn encoded(&mut self, encoding: u8) -> Result<u64, TranslateError> {
        let field_vaddr = self.current_vaddr();
        let raw = match encoding & FORMAT_MASK {
            0x00 | 0x04 | 0x0c => {
                i64::from_le_bytes(self.take(8)?.try_into().map_err(|_| bad("short read"))?)
            }
            0x01 => self.uleb()? as i64,
            0x02 => i64::from(u16::from_le_bytes(
                self.take(2)?.try_into().map_err(|_| bad("short read"))?,
            )),
            0x03 => i64::from(self.u32()?),
            0x09 => self.sleb()?,
            0x0a => i64::from(i16::from_le_bytes(
                self.take(2)?.try_into().map_err(|_| bad("short read"))?,
            )),
            0x0b => i64::from(self.u32()? as i32),
            _ => return Err(bad("unsupported pointer format")),
        };
        match encoding & APPLICATION_MASK {
            APPLICATION_NONE => Ok(raw as u64),
            APPLICATION_PCREL => Ok(field_vaddr.wrapping_add(raw as u64)),
            _ => Err(bad("unsupported pointer application")),
        }
    }
}

pub(super) fn read(
    data: &[u8],
    map: &AddressMap<'_>,
    phdrs: &[Phdr],
    endian: Endianness,
) -> Result<Vec<CodeRange>, TranslateError> {
    let Some(header) = phdrs
        .iter()
        .find(|ph| ph.p_type(endian) == elf::PT_GNU_EH_FRAME)
    else {
        return Ok(Vec::new());
    };
    let header_vaddr = header.p_vaddr(endian);
    let header_len = header.p_filesz(endian);
    let offset = map.file_offset(header_vaddr, header_len)?;
    let bytes = data
        .read_bytes_at(offset, header_len)
        .map_err(|()| bad("header is outside the file"))?;
    let mut reader = Reader::new(bytes, header_vaddr);
    let (version, pointer_encoding) = (reader.u8()?, reader.u8()?);
    if version != HEADER_VERSION {
        return Err(bad("unsupported header version"));
    }
    reader.take(2)?;
    if pointer_encoding == ENCODING_OMIT {
        return Ok(Vec::new());
    }
    let frames_vaddr = reader.encoded(pointer_encoding)?;
    read_frames(data, map, frames_vaddr)
}

/// Walks the CIE and FDE records sequentially until the zero terminator.
fn read_frames(
    data: &[u8],
    map: &AddressMap<'_>,
    frames_vaddr: u64,
) -> Result<Vec<CodeRange>, TranslateError> {
    let mut ranges = Vec::new();
    let mut cie_encodings: HashMap<u64, u8> = HashMap::new();
    let mut vaddr = frames_vaddr;
    for _ in 0..MAX_ENTRIES {
        let length_offset = map.file_offset(vaddr, 4)?;
        let length_bytes = data
            .read_bytes_at(length_offset, 4)
            .map_err(|()| bad("entry is outside the file"))?;
        let length = u32::from_le_bytes(length_bytes.try_into().map_err(|_| bad("short read"))?);
        if length == 0 {
            return Ok(ranges);
        }
        if length == EXTENDED_LENGTH_MARKER {
            return Err(bad("64-bit entry lengths are not supported"));
        }
        let total = add(u64::from(length), 4)?;
        let offset = map.file_offset(vaddr, total)?;
        let entry = data
            .read_bytes_at(offset, total)
            .map_err(|()| bad("entry is outside the file"))?;
        let mut reader = Reader::new(entry, vaddr);
        reader.take(4)?;
        let id_vaddr = reader.current_vaddr();
        let id = reader.u32()?;
        if id == 0 {
            let encoding = parse_cie(&mut reader)?;
            cie_encodings.insert(vaddr, encoding);
        } else {
            let cie_vaddr = id_vaddr
                .checked_sub(u64::from(id))
                .ok_or_else(|| bad("bad CIE pointer"))?;
            let encoding = match cie_encodings.get(&cie_vaddr) {
                Some(&encoding) => encoding,
                None => parse_cie_at(data, map, cie_vaddr)?,
            };
            ranges.push(parse_fde(&mut reader, encoding)?);
        }
        vaddr = add(vaddr, total)?;
    }
    Err(TranslateError::LimitExceeded(
        "too many .eh_frame entries".into(),
    ))
}

fn parse_cie_at(data: &[u8], map: &AddressMap<'_>, vaddr: u64) -> Result<u8, TranslateError> {
    let offset = map.file_offset(vaddr, 8)?;
    let head = data
        .read_bytes_at(offset, 8)
        .map_err(|()| bad("CIE is outside the file"))?;
    let length = u32::from_le_bytes(head[0..4].try_into().map_err(|_| bad("short read"))?);
    let total = add(u64::from(length), 4)?;
    let entry_offset = map.file_offset(vaddr, total)?;
    let entry = data
        .read_bytes_at(entry_offset, total)
        .map_err(|()| bad("CIE is outside the file"))?;
    let mut reader = Reader::new(entry, vaddr);
    reader.take(8)?;
    parse_cie(&mut reader)
}

/// Returns the FDE pointer encoding declared by the CIE (`R` augmentation), defaulting to absolute.
fn parse_cie(reader: &mut Reader<'_>) -> Result<u8, TranslateError> {
    let version = reader.u8()?;
    let mut augmentation = Vec::new();
    loop {
        match reader.u8()? {
            0 => break,
            byte => augmentation.push(byte),
        }
        if augmentation.len() > 16 {
            return Err(bad("augmentation string is too long"));
        }
    }
    if version == 4 {
        reader.take(2)?;
    }
    reader.uleb()?;
    reader.sleb()?;
    if version == 1 {
        reader.u8()?;
    } else {
        reader.uleb()?;
    }
    if augmentation.first() != Some(&b'z') {
        return Ok(0);
    }
    reader.uleb()?;
    let mut encoding = 0;
    for &character in &augmentation[1..] {
        match character {
            b'R' => encoding = reader.u8()?,
            b'L' => {
                reader.u8()?;
            }
            b'P' => {
                let personality_encoding = reader.u8()?;
                reader.encoded(personality_encoding)?;
            }
            b'S' | b'B' => {}
            _ => return Err(bad("unknown augmentation")),
        }
    }
    Ok(encoding)
}

fn parse_fde(reader: &mut Reader<'_>, encoding: u8) -> Result<CodeRange, TranslateError> {
    let start = reader.encoded(encoding)?;
    let len = reader.encoded(encoding & FORMAT_MASK)?;
    Ok(CodeRange { start, len })
}

#[cfg(test)]
mod tests {
    use crate::analyze::analyze;

    const TLS_BASIC: &[u8] = include_bytes!("../../tests/fixtures/tls_basic/tls_basic");
    const LIBTLS: &[u8] = include_bytes!("../../tests/fixtures/tls_basic/libtls.so.1");

    #[test]
    fn executable_functions_cover_known_canary_sites() {
        let info = analyze(TLS_BASIC).expect("parses");
        assert!(
            info.code_ranges.len() >= 4,
            "main, print_number and friends"
        );
        for site in [0x10adu64, 0x1251, 0x1378, 0x1437] {
            assert!(
                info.code_ranges
                    .iter()
                    .any(|r| site >= r.start && site < r.start + r.len),
                "{site:#x} lies in a function"
            );
        }
    }

    #[test]
    fn ranges_never_cover_the_elf_header_or_data() {
        let info = analyze(LIBTLS).expect("parses");
        assert!(info
            .code_ranges
            .iter()
            .all(|r| r.start >= 0x1000 && r.len > 0));
    }

    #[test]
    fn libraries_report_ranges_too() {
        let info = analyze(LIBTLS).expect("parses");
        assert!(info
            .code_ranges
            .iter()
            .any(|r| 0x1124 >= r.start && 0x1124 < r.start + r.len));
    }
}
