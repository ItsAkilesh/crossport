// File: translator/src/bundle.rs
// Purpose: Write the translated files and the shim DLL into a loose bundle directory
// Author: Akilesh
// Date: 2026-10-29

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

/// Name of the file the shim reads at start to set the environment a launcher script would have set.
pub const ENV_FILE: &str = "crossport.env";

/// Writes `crossport.env` beside the executable: one `NAME=value` line per variable, `${BUNDLE}` standing
/// for the bundle directory. Names and values may not contain line breaks, and names no `=`.
pub fn write_env_file(
    out_dir: &Path,
    variables: &[(&str, &str)],
) -> Result<PathBuf, TranslateError> {
    let mut text =
        String::from("# Written by crossport convert; read by the shim at program start.\n");
    for (name, value) in variables {
        let is_valid =
            !name.is_empty() && !name.contains(['=', '\n', '\r']) && !value.contains(['\n', '\r']);
        if !is_valid {
            return Err(TranslateError::Unsupported(format!(
                "cannot write the environment variable {name:?}"
            )));
        }
        text.push_str(&format!("{name}={value}\n"));
    }
    let path = out_dir.join(ENV_FILE);
    fs::write(&path, text).map_err(io_error(format!("writing {}", path.display())))?;
    Ok(path)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_env_file_has_one_line_per_variable() {
        let dir = std::env::temp_dir().join("crossport-env-test");
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).expect("temp dir");
        let path =
            write_env_file(&dir, &[("APPDIR", "${BUNDLE}/appdir"), ("A", "b")]).expect("writes");
        let text = fs::read_to_string(path).expect("exists");
        assert!(text.contains("APPDIR=${BUNDLE}/appdir\nA=b\n"));
        assert!(write_env_file(&dir, &[("BAD=NAME", "x")]).is_err());
        assert!(write_env_file(&dir, &[("OK", "two\nlines")]).is_err());
    }

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
