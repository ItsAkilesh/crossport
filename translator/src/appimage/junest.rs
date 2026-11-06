// File: translator/src/appimage/junest.rs
// Purpose: Recognise an AppImage whose AppRun starts a JuNest root and make that root the AppDir
// Author: Akilesh
// Date: 2026-11-06
//
// A JuNest AppImage (the "archimage" builds, for example) holds a whole Arch Linux root in `.junest` and an
// AppRun script that runs the real program inside it with bubblewrap or proot. Crossport runs no scripts and
// no sandbox, but the root is laid out like an AppDir (`usr/bin`, `usr/lib`, `usr/share`), so its children are
// moved up beside AppRun and everything after that treats the image as an ordinary AppDir.

use std::fs;
use std::path::Path;

use crate::error::TranslateError;

const JUNEST_DIRECTORY: &str = ".junest";

/// Moves the children of `.junest` into `root`. Returns false when the image has no `.junest` directory.
/// A name that exists on both sides is an error: merging would hide one of them.
pub(super) fn hoist_root(root: &Path) -> Result<bool, TranslateError> {
    let junest = root.join(JUNEST_DIRECTORY);
    if !junest.is_dir() {
        return Ok(false);
    }
    let entries = fs::read_dir(&junest).map_err(|e| io_error(&junest, e))?;
    for entry in entries {
        let entry = entry.map_err(|e| io_error(&junest, e))?;
        let target = root.join(entry.file_name());
        if target.exists() {
            return Err(TranslateError::Unsupported(format!(
                "the JuNest root and the AppDir both hold {}",
                entry.file_name().to_string_lossy()
            )));
        }
        fs::rename(entry.path(), &target).map_err(|e| io_error(&target, e))?;
    }
    fs::remove_dir(&junest).map_err(|e| io_error(&junest, e))?;
    Ok(true)
}

fn io_error(path: &Path, source: std::io::Error) -> TranslateError {
    TranslateError::Io {
        context: format!("moving the JuNest root at {}", path.display()),
        source,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp_dir(name: &str) -> std::path::PathBuf {
        let dir = std::env::temp_dir().join(format!("crossport-junest-{name}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).expect("creates");
        dir
    }

    #[test]
    fn the_children_of_the_root_move_up_beside_apprun() {
        let root = temp_dir("hoist");
        fs::create_dir_all(root.join(".junest/usr/bin")).expect("creates");
        fs::write(root.join(".junest/usr/bin/app"), b"x").expect("writes");
        fs::write(root.join("AppRun"), b"#!/bin/sh").expect("writes");
        assert!(hoist_root(&root).expect("hoists"));
        assert!(root.join("usr/bin/app").is_file());
        assert!(root.join("AppRun").is_file());
        assert!(!root.join(".junest").exists());
    }

    #[test]
    fn an_image_without_a_root_is_left_alone() {
        let root = temp_dir("none");
        assert!(!hoist_root(&root).expect("checks"));
    }

    #[test]
    fn a_name_on_both_sides_is_refused() {
        let root = temp_dir("clash");
        fs::create_dir_all(root.join(".junest/usr")).expect("creates");
        fs::create_dir_all(root.join("usr")).expect("creates");
        assert!(hoist_root(&root).is_err());
        assert!(root.join(".junest/usr").is_dir());
    }
}
