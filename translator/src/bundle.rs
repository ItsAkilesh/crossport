// File: translator/src/bundle.rs
// Purpose: Write the translated executable and the shim DLL into a loose bundle directory
// Author: Akilesh
// Date: 2026-10-28

use std::fs;
use std::path::{Path, PathBuf};

use crate::error::TranslateError;
use crate::relink::SHIM_DLL;

fn io_error(context: String) -> impl FnOnce(std::io::Error) -> TranslateError {
    move |source| TranslateError::Io { context, source }
}

/// Creates `out_dir`, writes `<exe_name>.exe` and copies the shim beside it.
/// Returns the path of the executable.
pub fn write_bundle(
    exe_name: &str,
    exe_bytes: &[u8],
    shim_path: &Path,
    out_dir: &Path,
) -> Result<PathBuf, TranslateError> {
    fs::create_dir_all(out_dir).map_err(io_error(format!("creating {}", out_dir.display())))?;
    let exe_path = out_dir.join(format!("{exe_name}.exe"));
    fs::write(&exe_path, exe_bytes).map_err(io_error(format!("writing {}", exe_path.display())))?;
    let shim_target = out_dir.join(SHIM_DLL);
    fs::copy(shim_path, &shim_target)
        .map_err(io_error(format!("copying shim {}", shim_path.display())))?;
    Ok(exe_path)
}
