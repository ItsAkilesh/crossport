// File: translator/src/source.rs
// Purpose: Where bundled libraries come from: a trait for dependency injection and a directory-search implementation
// Author: Akilesh
// Date: 2026-10-28

use std::fs::File;
use std::io::Read;
use std::path::PathBuf;

use crate::error::TranslateError;
use crate::naming::ModuleName;

/// Largest ELF file the translator will read.
pub const MAX_ELF_BYTES: u64 = 256 * 1024 * 1024;

pub trait LibrarySource {
    /// Returns the bytes of the named library, or `MissingLibrary` when absent.
    fn load(&self, name: &ModuleName) -> Result<Vec<u8>, TranslateError>;
}

/// Searches the given directories in order, like `LD_LIBRARY_PATH`.
pub struct DirectorySource {
    directories: Vec<PathBuf>,
}

impl DirectorySource {
    pub fn new(directories: Vec<PathBuf>) -> Self {
        Self { directories }
    }
}

fn io_error(context: String, source: std::io::Error) -> TranslateError {
    TranslateError::Io { context, source }
}

impl LibrarySource for DirectorySource {
    fn load(&self, name: &ModuleName) -> Result<Vec<u8>, TranslateError> {
        for directory in &self.directories {
            let path = directory.join(name.as_str());
            let file = match File::open(&path) {
                Ok(file) => file,
                Err(error) if error.kind() == std::io::ErrorKind::NotFound => continue,
                Err(error) => return Err(io_error(format!("opening {}", path.display()), error)),
            };
            return read_bounded(file, &path.display().to_string());
        }
        Err(TranslateError::MissingLibrary { name: name.as_str().to_string() })
    }
}

/// Reads at most `MAX_ELF_BYTES`, failing instead of truncating when the file is larger.
pub fn read_bounded(file: File, label: &str) -> Result<Vec<u8>, TranslateError> {
    let mut bytes = Vec::new();
    file.take(MAX_ELF_BYTES + 1)
        .read_to_end(&mut bytes)
        .map_err(|error| io_error(format!("reading {label}"), error))?;
    if bytes.len() as u64 > MAX_ELF_BYTES {
        return Err(TranslateError::LimitExceeded(format!(
            "{label} is larger than {MAX_ELF_BYTES} bytes"
        )));
    }
    Ok(bytes)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    fn scratch(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("crossport-source-{name}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).expect("temp dir is creatable");
        dir
    }

    #[test]
    fn finds_a_library_in_a_later_directory() {
        let first = scratch("first");
        let second = scratch("second");
        fs::write(second.join("libx.so.1"), b"payload").expect("writable");
        let source = DirectorySource::new(vec![first, second]);
        let name = ModuleName::parse("libx.so.1").expect("valid");
        assert_eq!(source.load(&name).expect("found"), b"payload");
    }

    #[test]
    fn earlier_directory_wins() {
        let first = scratch("win-first");
        let second = scratch("win-second");
        fs::write(first.join("liby.so"), b"one").expect("writable");
        fs::write(second.join("liby.so"), b"two").expect("writable");
        let source = DirectorySource::new(vec![first, second]);
        let name = ModuleName::parse("liby.so").expect("valid");
        assert_eq!(source.load(&name).expect("found"), b"one");
    }

    #[test]
    fn missing_library_is_a_typed_error() {
        let source = DirectorySource::new(vec![scratch("empty")]);
        let name = ModuleName::parse("libnone.so").expect("valid");
        assert!(matches!(source.load(&name), Err(TranslateError::MissingLibrary { .. })));
    }
}
