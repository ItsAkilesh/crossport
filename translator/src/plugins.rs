// File: translator/src/plugins.rs
// Purpose: Find the plugin libraries a program opens with dlopen, in directories named on the command line
// Author: Akilesh
// Date: 2026-11-06

use std::fs::{self, File};
use std::io::Read;
use std::path::{Path, PathBuf};

use crate::error::TranslateError;
use crate::graph::PluginFile;
use crate::naming::ModuleName;
use crate::source::read_bounded;

/// More plugins than this is a sign of the wrong directory (the AppDir root, say), not of a program.
pub const MAX_PLUGINS: usize = 128;
const MAX_DEPTH: usize = 8;
const ELF_MAGIC: [u8; 4] = [0x7f, b'E', b'L', b'F'];
const SHARED_OBJECT_MARKER: &str = ".so";

/// Where toolkits keep the plugins they open with `dlopen`, relative to the AppDir: Qt platforms and styles,
/// GTK input methods and modules, GIO modules, gdk-pixbuf loaders and an application's own plugins.
const TOOLKIT_PLUGIN_DIRS: &[&str] = &[
    "usr/plugins",
    "usr/lib/gtk-3.0",
    "usr/lib/gio",
    "usr/lib/gdk-pixbuf-2.0",
    "usr/lib/gedit/plugins",
];

/// The toolkit plugin directories that exist in an unpacked AppDir.
pub fn toolkit_dirs(appdir: &Path) -> Vec<PathBuf> {
    TOOLKIT_PLUGIN_DIRS
        .iter()
        .map(|dir| appdir.join(dir))
        .filter(|path| path.is_dir())
        .collect()
}

/// Every ELF shared object (a file whose name contains `.so`) under the directories, in a stable order.
/// Symbolic links are not followed, so a hostile AppDir cannot point the scan elsewhere.
pub fn discover(dirs: &[PathBuf]) -> Result<Vec<PluginFile>, TranslateError> {
    let mut found = Vec::new();
    for dir in dirs {
        collect(dir, 0, &mut found)?;
    }
    found.sort();
    if found.len() > MAX_PLUGINS {
        return Err(TranslateError::LimitExceeded(format!(
            "more than {MAX_PLUGINS} plugins; point --plugin-dir at the plugin directory itself"
        )));
    }
    found.into_iter().map(|path| read_plugin(&path)).collect()
}

fn collect(dir: &Path, depth: usize, found: &mut Vec<PathBuf>) -> Result<(), TranslateError> {
    if depth > MAX_DEPTH {
        return Ok(());
    }
    let entries = fs::read_dir(dir).map_err(|source| TranslateError::Io {
        context: format!("reading {}", dir.display()),
        source,
    })?;
    for entry in entries {
        let entry = entry.map_err(|source| TranslateError::Io {
            context: format!("reading {}", dir.display()),
            source,
        })?;
        let Ok(kind) = entry.file_type() else {
            continue;
        };
        let path = entry.path();
        if kind.is_dir() {
            collect(&path, depth + 1, found)?;
        } else if kind.is_file() && is_shared_object(&path) {
            found.push(path);
        }
    }
    Ok(())
}

fn is_shared_object(path: &Path) -> bool {
    let named_so = path
        .file_name()
        .and_then(|name| name.to_str())
        .is_some_and(|name| name.contains(SHARED_OBJECT_MARKER));
    named_so && starts_with_elf_magic(path)
}

fn starts_with_elf_magic(path: &Path) -> bool {
    let mut magic = [0u8; 4];
    File::open(path)
        .and_then(|mut file| file.read_exact(&mut magic))
        .is_ok()
        && magic == ELF_MAGIC
}

fn read_plugin(path: &Path) -> Result<PluginFile, TranslateError> {
    let io_error = |context: String, source| TranslateError::Io { context, source };
    let file_name = path
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("");
    let file = File::open(path).map_err(|e| io_error(format!("opening {}", path.display()), e))?;
    let origin = path
        .parent()
        .map(Path::canonicalize)
        .transpose()
        .map_err(|e| io_error(format!("resolving {}", path.display()), e))?;
    Ok(PluginFile {
        name: ModuleName::parse(file_name)?,
        data: read_bounded(file, &path.display().to_string())?,
        origin,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    const ELF: &[u8] = include_bytes!("../tests/fixtures/two_libs/libgreet.so.1");

    fn scratch(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("crossport-plugins-{name}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).expect("temp dir is creatable");
        dir
    }

    #[test]
    fn finds_shared_objects_in_nested_directories_in_a_stable_order() {
        let root = scratch("nested");
        fs::create_dir_all(root.join("platforms")).expect("creates");
        fs::write(root.join("platforms/libb.so"), ELF).expect("writes");
        fs::write(root.join("liba.so.1"), ELF).expect("writes");
        let names: Vec<String> = discover(&[root])
            .expect("discovers")
            .iter()
            .map(|plugin| plugin.name.as_str().to_string())
            .collect();
        assert_eq!(names, ["liba.so.1", "libb.so"]);
    }

    #[test]
    fn ignores_files_that_are_not_elf_or_not_shared_objects() {
        let root = scratch("ignored");
        fs::write(root.join("notes.txt"), ELF).expect("writes");
        fs::write(root.join("libfake.so"), b"text, not ELF").expect("writes");
        assert!(discover(&[root]).expect("discovers").is_empty());
    }

    #[test]
    fn only_the_toolkit_directories_that_exist_are_listed() {
        let root = scratch("toolkit");
        fs::create_dir_all(root.join("usr/lib/gio")).expect("creates");
        fs::create_dir_all(root.join("usr/plugins")).expect("creates");
        assert_eq!(
            toolkit_dirs(&root),
            [root.join("usr/plugins"), root.join("usr/lib/gio")]
        );
    }

    #[test]
    fn a_missing_directory_is_an_error() {
        let result = discover(&[PathBuf::from("/definitely/not/here")]);
        assert!(matches!(result, Err(TranslateError::Io { .. })));
    }

    #[test]
    fn too_many_plugins_is_an_error() {
        let root = scratch("many");
        for index in 0..=MAX_PLUGINS {
            fs::write(root.join(format!("lib{index}.so")), ELF).expect("writes");
        }
        assert!(matches!(
            discover(&[root]),
            Err(TranslateError::LimitExceeded(_))
        ));
    }
}
