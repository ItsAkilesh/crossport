// File: translator/src/source.rs
// Purpose: Where bundled libraries come from: a trait for dependency injection, a directory list and an AppDir search
// Author: Akilesh
// Date: 2026-10-28

use std::fs::{self, File};
use std::io::Read;
use std::path::{Path, PathBuf};

use crate::error::TranslateError;
use crate::naming::ModuleName;
use crate::search::search_order;

/// Largest ELF file the translator will read.
pub const MAX_ELF_BYTES: u64 = 256 * 1024 * 1024;

/// What a requesting module tells the loader about where to look.
#[derive(Debug, Default, Clone)]
pub struct SearchContext {
    /// Directory holding the requesting module, for `$ORIGIN`.
    pub origin: Option<PathBuf>,
    pub rpath: Vec<String>,
    pub runpath: Vec<String>,
}

pub struct LoadedLibrary {
    pub bytes: Vec<u8>,
    /// Directory the file was read from: the `$ORIGIN` of the library's own dependencies.
    pub origin: Option<PathBuf>,
}

pub trait LibrarySource {
    /// Returns the named library, or `MissingLibrary` when absent.
    fn load(
        &self,
        name: &ModuleName,
        context: &SearchContext,
    ) -> Result<LoadedLibrary, TranslateError>;

    /// Directory of the executable being translated, when the source knows it.
    fn executable_origin(&self) -> Option<PathBuf> {
        None
    }
}

/// Searches the given directories in order, like `LD_LIBRARY_PATH`, ignoring `RPATH` and `RUNPATH`.
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

/// Reads `directory/name` if it is a regular file there; `Ok(None)` when it is absent or is not
/// a file (a directory with the library's name is skipped, not an error). When `confine_to` is
/// given, a path that resolves, through symlinks, outside that root is skipped too.
fn read_candidate(
    directory: &Path,
    name: &ModuleName,
    confine_to: Option<&Path>,
) -> Result<Option<LoadedLibrary>, TranslateError> {
    let path = directory.join(name.as_str());
    match fs::metadata(&path) {
        Ok(metadata) if metadata.is_file() => {}
        Ok(_) => return Ok(None),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(None),
        Err(error) => return Err(io_error(format!("inspecting {}", path.display()), error)),
    }
    if let Some(root) = confine_to {
        if !resolves_inside(&path, root)? {
            return Ok(None);
        }
    }
    let file = File::open(&path)
        .map_err(|error| io_error(format!("opening {}", path.display()), error))?;
    let bytes = read_bounded(file, &path.display().to_string())?;
    Ok(Some(LoadedLibrary {
        bytes,
        origin: Some(directory.to_path_buf()),
    }))
}

fn resolves_inside(path: &Path, root: &Path) -> Result<bool, TranslateError> {
    let canonical =
        fs::canonicalize(path).map_err(|e| io_error(format!("resolving {}", path.display()), e))?;
    let canonical_root =
        fs::canonicalize(root).map_err(|e| io_error(format!("resolving {}", root.display()), e))?;
    Ok(canonical.starts_with(canonical_root))
}

fn missing(name: &ModuleName) -> TranslateError {
    TranslateError::MissingLibrary {
        name: name.as_str().to_string(),
    }
}

impl LibrarySource for DirectorySource {
    fn load(
        &self,
        name: &ModuleName,
        _context: &SearchContext,
    ) -> Result<LoadedLibrary, TranslateError> {
        for directory in &self.directories {
            if let Some(library) = read_candidate(directory, name, None)? {
                return Ok(library);
            }
        }
        Err(missing(name))
    }
}

/// Searches an unpacked AppDir the way the dynamic loader would: the requester's `RPATH`
/// (when it has no `RUNPATH`), extra directories, its `RUNPATH` with `$ORIGIN`, then the
/// default library directories. Nothing outside the AppDir is read, except the runtime directories
/// the user names themselves (the host libraries an AppImage expects, such as libstdc++), which are
/// searched last.
pub struct AppDirSource {
    root: PathBuf,
    executable: PathBuf,
    extra: Vec<Vec<String>>,
    runtime: Vec<PathBuf>,
}

impl AppDirSource {
    /// `executable` must lie inside `root`; `extra_directories` are also relative to `root`.
    pub fn new(
        root: PathBuf,
        executable: PathBuf,
        extra_directories: &[PathBuf],
    ) -> Result<Self, TranslateError> {
        let relative_to_root = |path: &Path| -> Result<Vec<String>, TranslateError> {
            let inside = path.strip_prefix(&root).map_err(|_| {
                TranslateError::Usage(format!(
                    "{} is not inside the AppDir {}",
                    path.display(),
                    root.display()
                ))
            })?;
            Ok(inside
                .components()
                .map(|c| c.as_os_str().to_string_lossy().into_owned())
                .collect())
        };
        let extra = extra_directories
            .iter()
            .map(|d| relative_to_root(d))
            .collect::<Result<Vec<_>, _>>()?;
        relative_to_root(&executable)?;
        Ok(Self {
            root,
            executable,
            extra,
            runtime: Vec::new(),
        })
    }

    /// Directories outside the AppDir searched after everything inside it.
    pub fn with_runtime_directories(mut self, directories: Vec<PathBuf>) -> Self {
        self.runtime = directories;
        self
    }

    fn origin_components(&self, origin: &Path) -> Option<Vec<String>> {
        let inside = origin.strip_prefix(&self.root).ok()?;
        Some(
            inside
                .components()
                .map(|c| c.as_os_str().to_string_lossy().into_owned())
                .collect(),
        )
    }
}

impl LibrarySource for AppDirSource {
    fn load(
        &self,
        name: &ModuleName,
        context: &SearchContext,
    ) -> Result<LoadedLibrary, TranslateError> {
        let origin = context
            .origin
            .as_deref()
            .and_then(|o| self.origin_components(o));
        let directories = search_order(
            &context.rpath,
            &context.runpath,
            &self.extra,
            origin.as_deref(),
        );
        for components in directories {
            let directory = components
                .iter()
                .fold(self.root.clone(), |path, part| path.join(part));
            if let Some(library) = read_candidate(&directory, name, Some(&self.root))? {
                return Ok(library);
            }
        }
        for directory in &self.runtime {
            if let Some(library) = read_candidate(directory, name, None)? {
                return Ok(library);
            }
        }
        Err(missing(name))
    }

    fn executable_origin(&self) -> Option<PathBuf> {
        self.executable.parent().map(Path::to_path_buf)
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
        assert_eq!(
            source
                .load(&name, &SearchContext::default())
                .expect("found")
                .bytes,
            b"payload"
        );
    }

    #[test]
    fn earlier_directory_wins() {
        let first = scratch("win-first");
        let second = scratch("win-second");
        fs::write(first.join("liby.so"), b"one").expect("writable");
        fs::write(second.join("liby.so"), b"two").expect("writable");
        let source = DirectorySource::new(vec![first, second]);
        let name = ModuleName::parse("liby.so").expect("valid");
        assert_eq!(
            source
                .load(&name, &SearchContext::default())
                .expect("found")
                .bytes,
            b"one"
        );
    }

    #[test]
    fn missing_library_is_a_typed_error() {
        let source = DirectorySource::new(vec![scratch("empty")]);
        let name = ModuleName::parse("libnone.so").expect("valid");
        assert!(matches!(
            source.load(&name, &SearchContext::default()),
            Err(TranslateError::MissingLibrary { .. })
        ));
    }
}
