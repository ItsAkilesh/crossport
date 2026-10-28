// File: tools/difftest/src/snapshot.rs
// Purpose: Record every regular file under a directory (path, size, SHA-256) so produced files can be compared
// Author: Akilesh
// Date: 2026-10-28

use std::collections::BTreeMap;
use std::fs::{self, File};
use std::io::Read;
use std::path::Path;

use sha2::{Digest, Sha256};

use crate::error::DifftestError;
use crate::result::FileRecord;

const MAX_FILES: usize = 10_000;
const MAX_DEPTH: usize = 64;
const MAX_FILE_BYTES: u64 = 512 * 1024 * 1024;
const READ_CHUNK_BYTES: usize = 64 * 1024;

fn io_error(context: String, source: std::io::Error) -> DifftestError {
    DifftestError::Io { context, source }
}

/// Walks `root` without following symlinks (so it cannot be led outside the
/// directory or into a cycle) and records each regular file.
pub fn snapshot(root: &Path) -> Result<BTreeMap<String, FileRecord>, DifftestError> {
    let mut files = BTreeMap::new();
    visit(root, "", 0, &mut files)?;
    Ok(files)
}

fn visit(
    directory: &Path,
    prefix: &str,
    depth: usize,
    files: &mut BTreeMap<String, FileRecord>,
) -> Result<(), DifftestError> {
    if depth > MAX_DEPTH {
        return Err(DifftestError::Usage(format!(
            "directory nesting deeper than {MAX_DEPTH}"
        )));
    }
    let entries = fs::read_dir(directory)
        .map_err(|e| io_error(format!("listing {}", directory.display()), e))?;
    for entry in entries {
        let entry = entry.map_err(|e| io_error(format!("listing {}", directory.display()), e))?;
        let name = entry.file_name().to_string_lossy().into_owned();
        let relative = if prefix.is_empty() {
            name
        } else {
            format!("{prefix}/{name}")
        };
        let kind = entry
            .file_type()
            .map_err(|e| io_error(format!("inspecting {relative}"), e))?;
        if kind.is_dir() {
            visit(&entry.path(), &relative, depth + 1, files)?;
        } else if kind.is_file() {
            if files.len() >= MAX_FILES {
                return Err(DifftestError::Usage(format!(
                    "more than {MAX_FILES} files to record"
                )));
            }
            files.insert(relative.clone(), record(&entry.path(), &relative)?);
        }
    }
    Ok(())
}

fn record(path: &Path, label: &str) -> Result<FileRecord, DifftestError> {
    let mut file = File::open(path).map_err(|e| io_error(format!("opening {label}"), e))?;
    let mut hasher = Sha256::new();
    let mut buffer = vec![0u8; READ_CHUNK_BYTES];
    let mut size = 0u64;
    loop {
        let count = file
            .read(&mut buffer)
            .map_err(|e| io_error(format!("reading {label}"), e))?;
        if count == 0 {
            break;
        }
        size += count as u64;
        if size > MAX_FILE_BYTES {
            return Err(DifftestError::Usage(format!(
                "{label} is larger than {MAX_FILE_BYTES} bytes"
            )));
        }
        hasher.update(&buffer[..count]);
    }
    Ok(FileRecord {
        size,
        sha256: hex(&hasher.finalize()),
    })
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scratch(name: &str) -> std::path::PathBuf {
        let dir = std::env::temp_dir().join(format!("difftest-snapshot-{name}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).expect("temp dir");
        dir
    }

    #[test]
    fn hashes_files_in_nested_directories_with_slash_paths() {
        let dir = scratch("nested");
        fs::create_dir_all(dir.join("a/b")).expect("dirs");
        fs::write(dir.join("a/b/c.txt"), b"abc").expect("file");
        fs::write(dir.join("top.bin"), b"").expect("file");
        let files = snapshot(&dir).expect("snapshots");
        assert_eq!(files["a/b/c.txt"].size, 3);
        assert_eq!(
            files["a/b/c.txt"].sha256,
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
        assert_eq!(
            files["top.bin"].sha256,
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        );
        assert_eq!(files.len(), 2);
    }

    #[test]
    fn an_empty_directory_has_no_files() {
        assert!(snapshot(&scratch("empty")).expect("snapshots").is_empty());
    }

    #[test]
    fn a_missing_directory_is_an_error() {
        let missing = std::env::temp_dir().join("difftest-snapshot-missing-dir");
        let _ = fs::remove_dir_all(&missing);
        assert!(matches!(snapshot(&missing), Err(DifftestError::Io { .. })));
    }
}
