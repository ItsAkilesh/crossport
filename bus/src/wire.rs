// File: bus/src/wire.rs
// Purpose: D-Bus wire encoding: aligned reading and writing of basic values, and skipping any value by its signature
// Author: Akilesh
// Date: 2026-11-09

use std::fmt;

/// The messages the bus reads and writes are little-endian, as every client on x86-64 sends them.
pub const ENDIAN_LITTLE: u8 = b'l';
const SIGNATURE_MAX_DEPTH: usize = 32;

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub struct WireError(pub &'static str);

impl fmt::Display for WireError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "malformed message: {}", self.0)
    }
}

impl std::error::Error for WireError {}

pub fn align_up(position: usize, alignment: usize) -> usize {
    position.div_ceil(alignment) * alignment
}

/// Reads aligned values from a message; positions count from the start of the message.
pub struct Reader<'a> {
    data: &'a [u8],
    position: usize,
}

impl<'a> Reader<'a> {
    pub fn new(data: &'a [u8], position: usize) -> Self {
        Self { data, position }
    }

    pub fn position(&self) -> usize {
        self.position
    }

    pub fn align(&mut self, alignment: usize) -> Result<(), WireError> {
        let aligned = align_up(self.position, alignment);
        if aligned > self.data.len() {
            return Err(WireError("padding runs past the end"));
        }
        self.position = aligned;
        Ok(())
    }

    pub fn bytes(&mut self, count: usize) -> Result<&'a [u8], WireError> {
        let end = self
            .position
            .checked_add(count)
            .filter(|&end| end <= self.data.len())
            .ok_or(WireError("a value runs past the end"))?;
        let slice = &self.data[self.position..end];
        self.position = end;
        Ok(slice)
    }

    pub fn u8(&mut self) -> Result<u8, WireError> {
        Ok(self.bytes(1)?[0])
    }

    pub fn u32(&mut self) -> Result<u32, WireError> {
        self.align(4)?;
        let bytes = self.bytes(4)?;
        Ok(u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]))
    }

    /// A string or object path: length, the characters, and a NUL.
    pub fn string(&mut self) -> Result<&'a str, WireError> {
        let length = self.u32()? as usize;
        let text = self.bytes(length)?;
        if self.u8()? != 0 {
            return Err(WireError("a string is not terminated"));
        }
        std::str::from_utf8(text).map_err(|_| WireError("a string is not UTF-8"))
    }

    /// A signature: a one-byte length, the characters, and a NUL.
    pub fn signature(&mut self) -> Result<&'a str, WireError> {
        let length = usize::from(self.u8()?);
        let text = self.bytes(length)?;
        if self.u8()? != 0 {
            return Err(WireError("a signature is not terminated"));
        }
        std::str::from_utf8(text).map_err(|_| WireError("a signature is not UTF-8"))
    }

    /// Skips one value of the single complete type that starts `signature`; returns the rest of the signature.
    pub fn skip(&mut self, signature: &'a [u8]) -> Result<&'a [u8], WireError> {
        self.skip_at(signature, 0)
    }

    fn skip_at(&mut self, signature: &'a [u8], depth: usize) -> Result<&'a [u8], WireError> {
        if depth > SIGNATURE_MAX_DEPTH {
            return Err(WireError("types nest too deeply"));
        }
        let (&code, rest) = signature
            .split_first()
            .ok_or(WireError("a signature ends early"))?;
        match code {
            b'y' => self.bytes(1).map(|_| rest),
            b'n' | b'q' => self.fixed(2).map(|_| rest),
            b'b' | b'i' | b'u' | b'h' => self.fixed(4).map(|_| rest),
            b'x' | b't' | b'd' => self.fixed(8).map(|_| rest),
            b's' | b'o' => self.string().map(|_| rest),
            b'g' => self.signature().map(|_| rest),
            b'v' => {
                let inner = self.signature()?;
                self.skip_at(inner.as_bytes(), depth + 1)?;
                Ok(rest)
            }
            b'a' => self.skip_array(rest, depth),
            b'(' => self.skip_members(rest, b')', depth),
            b'{' => self.skip_members(rest, b'}', depth),
            _ => Err(WireError("an unknown type code")),
        }
    }

    fn fixed(&mut self, size: usize) -> Result<(), WireError> {
        self.align(size)?;
        self.bytes(size).map(|_| ())
    }

    fn skip_array(&mut self, element: &'a [u8], depth: usize) -> Result<&'a [u8], WireError> {
        let length = self.u32()? as usize;
        let after_type = type_end(element, depth)?;
        self.align(alignment_of(element[0]))?;
        let end = self
            .position
            .checked_add(length)
            .filter(|&end| end <= self.data.len())
            .ok_or(WireError("an array runs past the end"))?;
        while self.position < end {
            self.skip_at(element, depth + 1)?;
        }
        if self.position != end {
            return Err(WireError("an array's length is wrong"));
        }
        Ok(&element[after_type..])
    }

    fn skip_members(
        &mut self,
        members: &'a [u8],
        close: u8,
        depth: usize,
    ) -> Result<&'a [u8], WireError> {
        self.align(8)?;
        let mut rest = members;
        while rest.first() != Some(&close) {
            rest = self.skip_at(rest, depth + 1)?;
        }
        Ok(&rest[1..])
    }
}

fn alignment_of(code: u8) -> usize {
    match code {
        b'y' | b'g' | b'v' => 1,
        b'n' | b'q' => 2,
        b'x' | b't' | b'd' | b'(' | b'{' => 8,
        _ => 4,
    }
}

/// The length of the single complete type at the start of `signature`.
fn type_end(signature: &[u8], depth: usize) -> Result<usize, WireError> {
    if depth > SIGNATURE_MAX_DEPTH {
        return Err(WireError("types nest too deeply"));
    }
    match signature.first() {
        None => Err(WireError("a signature ends early")),
        Some(b'a') => Ok(1 + type_end(&signature[1..], depth + 1)?),
        Some(&open @ (b'(' | b'{')) => {
            let close = if open == b'(' { b')' } else { b'}' };
            let mut length = 1;
            while signature.get(length) != Some(&close) {
                length += type_end(&signature[length..], depth + 1)?;
            }
            Ok(length + 1)
        }
        Some(_) => Ok(1),
    }
}

/// Writes aligned values; positions count from the start of the message.
#[derive(Default)]
pub struct Writer {
    buffer: Vec<u8>,
}

impl Writer {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn len(&self) -> usize {
        self.buffer.len()
    }

    pub fn is_empty(&self) -> bool {
        self.buffer.is_empty()
    }

    pub fn into_bytes(self) -> Vec<u8> {
        self.buffer
    }

    pub fn align(&mut self, alignment: usize) {
        let aligned = align_up(self.buffer.len(), alignment);
        self.buffer.resize(aligned, 0);
    }

    pub fn u8(&mut self, value: u8) {
        self.buffer.push(value);
    }

    pub fn u32(&mut self, value: u32) {
        self.align(4);
        self.buffer.extend_from_slice(&value.to_le_bytes());
    }

    pub fn raw(&mut self, bytes: &[u8]) {
        self.buffer.extend_from_slice(bytes);
    }

    pub fn string(&mut self, text: &str) {
        self.u32(u32::try_from(text.len()).unwrap_or(u32::MAX));
        self.raw(text.as_bytes());
        self.u8(0);
    }

    pub fn signature(&mut self, text: &str) {
        self.u8(u8::try_from(text.len()).unwrap_or(u8::MAX));
        self.raw(text.as_bytes());
        self.u8(0);
    }

    /// Overwrites four bytes already written (a length that was not known yet).
    pub fn patch_u32(&mut self, at: usize, value: u32) {
        self.buffer[at..at + 4].copy_from_slice(&value.to_le_bytes());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn strings_and_numbers_are_aligned_and_read_back() {
        let mut writer = Writer::new();
        writer.u8(7);
        writer.string("hello");
        writer.signature("a{sv}");
        writer.u32(42);
        let bytes = writer.into_bytes();
        let mut reader = Reader::new(&bytes, 0);
        assert_eq!(reader.u8(), Ok(7));
        assert_eq!(reader.string(), Ok("hello"));
        assert_eq!(reader.signature(), Ok("a{sv}"));
        assert_eq!(reader.u32(), Ok(42));
    }

    #[test]
    fn a_string_must_end_in_nul_and_be_utf8() {
        assert!(Reader::new(&[1, 0, 0, 0, b'x', b'!'], 0).string().is_err());
        assert!(Reader::new(&[1, 0, 0, 0, 0xff, 0], 0).string().is_err());
    }

    #[test]
    fn reading_past_the_end_is_an_error_not_a_panic() {
        assert!(Reader::new(&[1, 0], 0).u32().is_err());
        assert!(Reader::new(&[200, 0, 0, 0], 0).string().is_err());
    }

    #[test]
    fn skipping_handles_arrays_dictionaries_structs_and_variants() {
        let mut writer = Writer::new();
        // a{sv} with one entry: "k" -> variant "s" "v"
        let array_length = writer.len();
        writer.u32(0);
        writer.align(8);
        let start = writer.len();
        writer.string("k");
        writer.signature("s");
        writer.string("v");
        let length = u32::try_from(writer.len() - start).expect("small");
        writer.patch_u32(array_length, length);
        writer.u32(99);
        let bytes = writer.into_bytes();
        let mut reader = Reader::new(&bytes, 0);
        assert_eq!(reader.skip(b"a{sv}u"), Ok(&b"u"[..]));
        assert_eq!(reader.u32(), Ok(99));
    }

    #[test]
    fn an_empty_array_still_pads_to_its_element_alignment() {
        let mut writer = Writer::new();
        writer.u32(0);
        writer.align(8);
        writer.u32(5);
        let bytes = writer.into_bytes();
        let mut reader = Reader::new(&bytes, 0);
        assert_eq!(reader.skip(b"a(uu)"), Ok(&b""[..]));
        assert_eq!(reader.position(), 8);
    }

    #[test]
    fn deeply_nested_types_are_refused() {
        let signature = vec![b'a'; SIGNATURE_MAX_DEPTH + 5];
        let bytes = [0u8; 64];
        assert!(Reader::new(&bytes, 0).skip(&signature).is_err());
    }
}
