// File: installer/src/payload.rs
// Purpose: The file archive appended to the setup program: pack a folder, find it again, unpack it safely
// Author: Akilesh
// Date: 2026-11-05

use std::fs;
use std::path::{Component, Path, PathBuf};

use miniz_oxide::deflate::compress_to_vec;
use miniz_oxide::inflate::decompress_to_vec_with_limit;

const MAGIC: &[u8; 8] = b"CPPAYLD1";
const TRAILER_BYTES: usize = 16;
const COMPRESSION_LEVEL: u8 = 6;
/// No single file in a payload may expand beyond this.
const MAX_FILE_BYTES: usize = 1 << 30;
const MAX_ENTRIES: u32 = 100_000;

/// One file to install: a relative path and its contents.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Entry {
    pub path: String,
    pub data: Vec<u8>,
}

/// Why a payload could not be read.
#[derive(Debug, PartialEq, Eq)]
pub enum PayloadError {
    Missing,
    Corrupt(&'static str),
    UnsafePath(String),
}

/// A relative path with no `..`, drive or root parts, in forward-slash form.
pub fn safe_relative(path: &str) -> bool {
    !path.is_empty()
        && !path.contains('\\')
        && Path::new(path).components().all(|c| matches!(c, Component::Normal(_)))
        && !path.contains(':')
}

/// Serialise a set of files (not yet attached to anything).
pub fn encode(entries: &[Entry]) -> Vec<u8> {
    let mut out = Vec::new();
    out.extend_from_slice(&(entries.len() as u32).to_le_bytes());
    for entry in entries {
        let packed = compress_to_vec(&entry.data, COMPRESSION_LEVEL);
        out.extend_from_slice(&(entry.path.len() as u32).to_le_bytes());
        out.extend_from_slice(entry.path.as_bytes());
        out.extend_from_slice(&(entry.data.len() as u64).to_le_bytes());
        out.extend_from_slice(&(packed.len() as u64).to_le_bytes());
        out.extend_from_slice(&packed);
    }
    out
}

/// `program` followed by the payload and its trailer: the setup file.
pub fn attach(program: &[u8], payload: &[u8]) -> Vec<u8> {
    let mut out = program.to_vec();
    out.extend_from_slice(payload);
    out.extend_from_slice(MAGIC);
    out.extend_from_slice(&(payload.len() as u64).to_le_bytes());
    out
}

/// The payload bytes at the end of a setup file.
pub fn find(file: &[u8]) -> Result<&[u8], PayloadError> {
    let trailer = file.len().checked_sub(TRAILER_BYTES).ok_or(PayloadError::Missing)?;
    if &file[trailer..trailer + 8] != MAGIC {
        return Err(PayloadError::Missing);
    }
    let mut length = [0u8; 8];
    length.copy_from_slice(&file[trailer + 8..]);
    let length = usize::try_from(u64::from_le_bytes(length)).map_err(|_| PayloadError::Corrupt("length"))?;
    let start = trailer.checked_sub(length).ok_or(PayloadError::Corrupt("length"))?;
    Ok(&file[start..trailer])
}

struct Cursor<'a>(&'a [u8]);

impl<'a> Cursor<'a> {
    fn take(&mut self, n: usize) -> Result<&'a [u8], PayloadError> {
        if n > self.0.len() {
            return Err(PayloadError::Corrupt("truncated"));
        }
        let (head, tail) = self.0.split_at(n);
        self.0 = tail;
        Ok(head)
    }
    fn u32(&mut self) -> Result<u32, PayloadError> {
        Ok(u32::from_le_bytes(self.take(4)?.try_into().map_err(|_| PayloadError::Corrupt("u32"))?))
    }
    fn u64(&mut self) -> Result<u64, PayloadError> {
        Ok(u64::from_le_bytes(self.take(8)?.try_into().map_err(|_| PayloadError::Corrupt("u64"))?))
    }
}

/// Read every file, checking paths and sizes.
pub fn decode(payload: &[u8]) -> Result<Vec<Entry>, PayloadError> {
    let mut cursor = Cursor(payload);
    let count = cursor.u32()?;
    if count > MAX_ENTRIES {
        return Err(PayloadError::Corrupt("too many entries"));
    }
    let mut entries = Vec::new();
    for _ in 0..count {
        let name_len = cursor.u32()? as usize;
        let path = String::from_utf8(cursor.take(name_len)?.to_vec()).map_err(|_| PayloadError::Corrupt("path"))?;
        if !safe_relative(&path) {
            return Err(PayloadError::UnsafePath(path));
        }
        let size = usize::try_from(cursor.u64()?).map_err(|_| PayloadError::Corrupt("size"))?;
        let packed_len = usize::try_from(cursor.u64()?).map_err(|_| PayloadError::Corrupt("size"))?;
        if size > MAX_FILE_BYTES {
            return Err(PayloadError::Corrupt("file too large"));
        }
        let data = decompress_to_vec_with_limit(cursor.take(packed_len)?, size)
            .map_err(|_| PayloadError::Corrupt("deflate"))?;
        if data.len() != size {
            return Err(PayloadError::Corrupt("size mismatch"));
        }
        entries.push(Entry { path, data });
    }
    Ok(entries)
}

/// Every file under `root`, as entries with forward-slash relative paths, in sorted order.
pub fn collect(root: &Path) -> std::io::Result<Vec<Entry>> {
    let mut found = Vec::new();
    walk(root, root, &mut found)?;
    found.sort_by(|a, b| a.path.cmp(&b.path));
    Ok(found)
}

fn walk(root: &Path, dir: &Path, found: &mut Vec<Entry>) -> std::io::Result<()> {
    for item in fs::read_dir(dir)? {
        let path = item?.path();
        if path.is_dir() {
            walk(root, &path, found)?;
        } else {
            let relative = path.strip_prefix(root).unwrap_or(&path);
            let parts: Vec<String> = relative.iter().map(|p| p.to_string_lossy().into_owned()).collect();
            found.push(Entry { path: parts.join("/"), data: fs::read(&path)? });
        }
    }
    Ok(())
}

/// Write entries below `dir`, returning the files created (relative, forward slashes).
pub fn extract(entries: &[Entry], dir: &Path) -> std::io::Result<Vec<PathBuf>> {
    let mut written = Vec::new();
    for entry in entries {
        let target = entry.path.split('/').fold(dir.to_path_buf(), |p, part| p.join(part));
        if let Some(parent) = target.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::write(&target, &entry.data)?;
        written.push(PathBuf::from(&entry.path));
    }
    Ok(written)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entry(path: &str, data: &[u8]) -> Entry {
        Entry { path: path.into(), data: data.to_vec() }
    }

    #[test]
    fn round_trips_through_a_setup_file() {
        let entries = vec![entry("crossport.exe", &[7u8; 5000]), entry("runtime/libc.so", b"x"), entry("empty", b"")];
        let file = attach(b"MZprogram", &encode(&entries));
        assert_eq!(decode(find(&file).unwrap()).unwrap(), entries);
    }

    #[test]
    fn a_plain_program_has_no_payload() {
        assert_eq!(find(b"MZprogram"), Err(PayloadError::Missing));
        assert_eq!(find(b""), Err(PayloadError::Missing));
    }

    #[test]
    fn rejects_unsafe_paths() {
        for bad in ["../x", "/abs", "C:/x", "a\\b", "a/../b", ""] {
            let payload = encode(&[entry(bad, b"x")]);
            assert!(decode(&payload).is_err(), "{bad}");
        }
    }

    #[test]
    fn rejects_truncated_and_lying_payloads() {
        let payload = encode(&[entry("a", &[1u8; 100])]);
        assert!(decode(&payload[..payload.len() - 3]).is_err());
        let mut lying = payload.clone();
        lying[4 + 4 + 1] = 5;
        assert!(decode(&lying).is_err());
    }

    #[test]
    fn a_bad_length_in_the_trailer_is_corrupt() {
        let mut file = attach(b"MZ", b"abc");
        let at = file.len() - 8;
        file[at..].copy_from_slice(&u64::MAX.to_le_bytes());
        assert!(matches!(find(&file), Err(PayloadError::Corrupt(_))));
    }

    #[test]
    fn collect_and_extract_round_trip_on_disk() {
        let base = std::env::temp_dir().join(format!("crossport-payload-{}", std::process::id()));
        let (src, dst) = (base.join("src"), base.join("dst"));
        fs::create_dir_all(src.join("sub")).unwrap();
        fs::write(src.join("a.txt"), "A").unwrap();
        fs::write(src.join("sub").join("b.txt"), "B").unwrap();
        let entries = collect(&src).unwrap();
        assert_eq!(entries.iter().map(|e| e.path.as_str()).collect::<Vec<_>>(), ["a.txt", "sub/b.txt"]);
        extract(&entries, &dst).unwrap();
        assert_eq!(fs::read_to_string(dst.join("sub").join("b.txt")).unwrap(), "B");
        fs::remove_dir_all(&base).unwrap();
    }
}
