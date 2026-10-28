// File: translator/src/bundle.rs
// Purpose: Write the translated files and the shim DLL into a loose bundle directory
// Author: Akilesh
// Date: 2026-10-28

use std::fs;
use std::path::{Path, PathBuf};

use crate::error::TranslateError;
use crate::shim_abi::SHIM_DLL;
use crate::translate::OutputFile;

fn io_error(context: String) -> impl FnOnce(std::io::Error) -> TranslateError {
    move |source| TranslateError::Io { context, source }
}

/// Creates `out_dir`, writes every file and copies the shim beside them.
/// Returns the path of the first file, which is the executable.
pub fn write_bundle(
    files: &[OutputFile],
    shim_path: &Path,
    out_dir: &Path,
) -> Result<PathBuf, TranslateError> {
    fs::create_dir_all(out_dir).map_err(io_error(format!("creating {}", out_dir.display())))?;
    let mut executable = None;
    for file in files {
        let path = out_dir.join(&file.file_name);
        fs::write(&path, &file.bytes).map_err(io_error(format!("writing {}", path.display())))?;
        executable.get_or_insert(path);
    }
    fs::copy(shim_path, out_dir.join(SHIM_DLL))
        .map_err(io_error(format!("copying shim {}", shim_path.display())))?;
    executable.ok_or_else(|| TranslateError::Elf("internal: translation produced no files".into()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn writes_files_and_copies_the_shim() {
        let dir = std::env::temp_dir().join("crossport-bundle-test");
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).expect("temp dir");
        let shim = dir.join("shim-source.dll");
        fs::write(&shim, b"shim").expect("writable");
        let out = dir.join("out");
        let files = [
            OutputFile {
                file_name: "app.exe".into(),
                bytes: b"exe".to_vec(),
            },
            OutputFile {
                file_name: "libx.so.1.dll".into(),
                bytes: b"dll".to_vec(),
            },
        ];
        let exe = write_bundle(&files, &shim, &out).expect("writes");
        assert_eq!(exe, out.join("app.exe"));
        assert_eq!(fs::read(out.join("libx.so.1.dll")).expect("exists"), b"dll");
        assert_eq!(fs::read(out.join(SHIM_DLL)).expect("exists"), b"shim");
    }
}
