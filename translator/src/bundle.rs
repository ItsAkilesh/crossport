// File: translator/src/bundle.rs
// Purpose: Write the translated files and the shim DLL into a loose bundle directory
// Author: Akilesh
// Date: 2026-11-09

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

/// The D-Bus session bus daemon (the `bus` crate), which programs in a bundle share through the shim.
pub const SESSION_BUS_EXE: &str = "crossport-bus.exe";
/// Names a session bus executable explicitly (a build tree keeps it apart from `crossport.exe`).
pub const SESSION_BUS_VARIABLE: &str = "CROSSPORT_BUS_EXE";

/// Where the session bus executable is: the variable if set, otherwise beside this program.
pub fn locate_session_bus() -> Option<PathBuf> {
    let explicit = std::env::var_os(SESSION_BUS_VARIABLE).map(PathBuf::from);
    let beside = std::env::current_exe()
        .ok()
        .and_then(|exe| exe.parent().map(|dir| dir.join(SESSION_BUS_EXE)));
    explicit
        .into_iter()
        .chain(beside)
        .find(|path| path.is_file())
}

/// Copies the session bus into the bundle when there is one to copy; returns whether it did. Without it a
/// program still runs, only without a D-Bus session.
pub fn copy_session_bus(out_dir: &Path, bus: Option<&Path>) -> Result<bool, TranslateError> {
    let Some(source) = bus else {
        return Ok(false);
    };
    let target = out_dir.join(SESSION_BUS_EXE);
    fs::copy(source, &target).map_err(io_error(format!("copying {}", source.display())))?;
    Ok(true)
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

/// Name of the fontconfig file `convert` writes, and the variable that tells fontconfig to read it.
pub const FONTS_CONF_FILE: &str = "fonts.conf";

/// Writes a fontconfig file beside the executable that lists `font_dirs` (Linux path form) and keeps the cache under
/// `/tmp`. A bundle has no `/etc/fonts`, and without a file fontconfig finds no fonts at all.
pub fn write_fonts_conf(out_dir: &Path, font_dirs: &[String]) -> Result<PathBuf, TranslateError> {
    if font_dirs
        .iter()
        .any(|dir| dir.contains(['<', '>', '&', '\n', '\r']))
    {
        return Err(TranslateError::Unsupported(
            "cannot write fonts.conf with markup or a line break in a directory".into(),
        ));
    }
    let mut text = String::from("<?xml version=\"1.0\"?>\n<fontconfig>\n");
    for dir in font_dirs {
        text.push_str(&format!("  <dir>{dir}</dir>\n"));
    }
    text.push_str("  <cachedir>/tmp/crossport-fontconfig</cachedir>\n</fontconfig>\n");
    let path = out_dir.join(FONTS_CONF_FILE);
    fs::write(&path, text).map_err(io_error(format!("writing {}", path.display())))?;
    Ok(path)
}

/// The directory of the system's fonts in Linux path form (`/C:/Windows/Fonts`), from the Windows directory.
pub fn windows_fonts_dir(windows_dir: &str) -> String {
    let slashed = windows_dir.replace('\\', "/");
    format!("/{}/Fonts", slashed.trim_end_matches('/'))
}

/// Name of the file Qt reads from the program's directory to find its plugins.
pub const QT_CONF_FILE: &str = "qt.conf";

/// Writes `qt.conf` beside the executable, pointing Qt at `plugins` (relative to the bundle). `QT_PLUGIN_PATH`
/// cannot do this: Qt splits it at every colon, and a Windows path has one after the drive letter.
pub fn write_qt_conf(
    out_dir: &Path,
    prefix: &str,
    plugins: &str,
) -> Result<PathBuf, TranslateError> {
    let is_valid = |text: &str| !text.contains(['\n', '\r']);
    if !is_valid(prefix) || !is_valid(plugins) {
        return Err(TranslateError::Unsupported(
            "cannot write qt.conf with a line break in a path".into(),
        ));
    }
    let text = format!("[Paths]\nPrefix = {prefix}\nPlugins = {plugins}\n");
    let path = out_dir.join(QT_CONF_FILE);
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
    fn the_session_bus_is_copied_when_there_is_one_and_skipped_when_there_is_not() {
        let dir = std::env::temp_dir().join("crossport-bus-copy-test");
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).expect("temp dir");
        let source = dir.join("source.exe");
        fs::write(&source, b"daemon").expect("writes");
        let out = dir.join("bundle");
        fs::create_dir_all(&out).expect("bundle dir");
        assert!(!copy_session_bus(&out, None).expect("no bus is fine"));
        assert!(!out.join(SESSION_BUS_EXE).exists());
        assert!(copy_session_bus(&out, Some(&source)).expect("copies"));
        assert_eq!(
            fs::read(out.join(SESSION_BUS_EXE)).expect("copied"),
            b"daemon"
        );
        assert!(copy_session_bus(&out, Some(&dir.join("missing.exe"))).is_err());
    }

    #[test]
    fn qt_conf_names_the_prefix_and_the_plugins() {
        let dir = std::env::temp_dir().join("crossport-qtconf-test");
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).expect("temp dir");
        let path = write_qt_conf(&dir, "appdir/usr", "plugins").expect("writes");
        assert_eq!(
            fs::read_to_string(path).expect("exists"),
            "[Paths]\nPrefix = appdir/usr\nPlugins = plugins\n"
        );
        assert!(write_qt_conf(&dir, "a\nb", "plugins").is_err());
    }

    #[test]
    fn fonts_conf_lists_the_directories() {
        let dir = std::env::temp_dir().join("crossport-fonts-test");
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).expect("temp dir");
        let path = write_fonts_conf(&dir, &["/C:/Windows/Fonts".to_string()]).expect("writes");
        let text = fs::read_to_string(path).expect("exists");
        assert!(text.contains("<dir>/C:/Windows/Fonts</dir>"));
        assert!(text.contains("<cachedir>/tmp/crossport-fontconfig</cachedir>"));
        assert!(write_fonts_conf(&dir, &["/a<b".to_string()]).is_err());
        assert_eq!(windows_fonts_dir(r"C:\Windows"), "/C:/Windows/Fonts");
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
