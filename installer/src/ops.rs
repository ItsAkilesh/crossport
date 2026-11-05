// File: installer/src/ops.rs
// Purpose: The two operations behind both the wizards and the silent modes: install an AppImage, install Crossport
// Author: Akilesh
// Date: 2026-11-05

use std::fs;
use std::path::{Path, PathBuf};

use crate::appimage_meta;
use crate::cli::{OpenSilent, SetupSilent};
use crate::engine::app::{install_app, AppRequest};
use crate::engine::selfinstall::{install_self, SelfOptions};
use crate::engine::{Env, Tools};
use crate::names::guess_app_name;
use crate::paths::{Placement, Roots, Scope};
use crate::payload::{decode, find, Entry};
use crate::report::Report;
use crate::win::{can_write, elevate::run_elevated, is_elevated, roots};

/// What happened to one AppImage install.
#[derive(Debug)]
pub struct OpenResult {
    pub report: Report,
    pub exe: Option<PathBuf>,
    pub dir: PathBuf,
}

/// The name shown for an AppImage: what the user typed, else its desktop entry, else its file name.
pub fn display_name(chosen: Option<&str>, meta_name: Option<&str>, image: &Path) -> String {
    let from_file = || guess_app_name(&image.file_name().map(|f| f.to_string_lossy().into_owned()).unwrap_or_default());
    chosen.or(meta_name).map(str::to_string).filter(|n| !n.trim().is_empty()).unwrap_or_else(from_file)
}

/// Where an install goes and whether it needs elevation. With no folder given, the default for the scope.
pub fn plan(dir: Option<&Path>, per_user: bool, default_dir: impl Fn(&Roots, Scope) -> Result<PathBuf, String>, elevated: bool) -> Result<(PathBuf, Placement), String> {
    let roots = roots().ok_or("could not find Program Files")?;
    let dir = match dir {
        Some(dir) => dir.to_path_buf(),
        None => default_dir(&roots, if per_user { Scope::User } else { Scope::Machine })?,
    };
    let placement = roots.place(&dir, can_write(&dir), elevated);
    Ok((dir, placement))
}

fn read_result(file: &Path) -> Result<(Option<PathBuf>, Report), String> {
    let text = fs::read_to_string(file).map_err(|_| "the administrator step did not report back".to_string())?;
    let (exe, report) = text.split_once('\n').ok_or("the administrator step wrote a damaged report")?;
    let report = Report::decode(report).ok_or("the administrator step wrote a damaged report")?;
    Ok(((!exe.is_empty()).then(|| PathBuf::from(exe)), report))
}

fn write_result(file: &Path, exe: Option<&Path>, report: &Report) {
    let exe = exe.map(|e| e.to_string_lossy().into_owned()).unwrap_or_default();
    let _ = fs::write(file, format!("{exe}\n{}", report.encode()));
}

fn temp_result_file() -> PathBuf {
    std::env::temp_dir().join(format!("crossport-result-{}.txt", std::process::id()))
}

/// Install an AppImage as an app, asking for elevation first when the folder needs it.
pub fn run_open(request: &OpenSilent, launcher: &Path, tools: &Tools) -> Result<OpenResult, String> {
    let meta = appimage_meta::read(&request.image).unwrap_or_default();
    let name = display_name(request.name.as_deref(), meta.name.as_deref(), &request.image);
    let default_dir = |roots: &Roots, scope| roots.app_dir(scope, &name).map_err(|_| "the app name is empty".to_string());
    let (dir, placement) = plan(request.dir.as_deref(), request.per_user, default_dir, is_elevated())?;
    if placement.needs_elevation {
        let file = temp_result_file();
        let relaunch = OpenSilent { name: Some(name), dir: Some(dir.clone()), result_file: Some(file.clone()), ..request.clone() };
        run_elevated(launcher, &relaunch.to_args())?;
        let (exe, report) = read_result(&file)?;
        let _ = fs::remove_file(&file);
        return Ok(OpenResult { report, exe, dir });
    }
    let app = AppRequest {
        image: &request.image,
        name: &name,
        dir: &dir,
        scope: placement.scope,
        start_menu: request.start_menu,
        desktop: request.desktop,
        stub_missing: request.stub_missing,
        icon_png: meta.icon_png.as_deref(),
        launcher,
    };
    let outcome = install_app(&app, tools, &Env::system(placement.scope)?)?;
    if let Some(file) = &request.result_file {
        write_result(file, outcome.exe.as_deref(), &outcome.report);
    }
    Ok(OpenResult { report: outcome.report, exe: outcome.exe, dir })
}

/// The files inside a setup program.
pub fn load_payload(setup_exe: &Path) -> Result<Vec<Entry>, String> {
    let bytes = fs::read(setup_exe).map_err(|e| format!("reading {}: {e}", setup_exe.display()))?;
    let payload = find(&bytes).map_err(|_| "this setup program has no files attached".to_string())?;
    decode(payload).map_err(|e| format!("the setup files are damaged: {e:?}"))
}

/// Install Crossport from the payload of `setup_exe`, elevating first when the folder needs it.
pub fn run_setup(request: &SetupSilent, setup_exe: &Path) -> Result<PathBuf, String> {
    let default_dir = |roots: &Roots, scope| Ok(roots.product_dir(scope));
    let (dir, placement) = plan(request.dir.as_deref(), request.per_user, default_dir, is_elevated())?;
    if placement.needs_elevation {
        let file = temp_result_file();
        let relaunch = SetupSilent { dir: Some(dir.clone()), result_file: Some(file.clone()), ..request.clone() };
        let code = run_elevated(setup_exe, &relaunch.to_args())?;
        let message = fs::read_to_string(&file).unwrap_or_default();
        let _ = fs::remove_file(&file);
        return if code == 0 { Ok(dir) } else { Err(if message.is_empty() { "the install failed".into() } else { message }) };
    }
    let entries = load_payload(setup_exe)?;
    let options = SelfOptions {
        dir: dir.clone(),
        scope: placement.scope,
        start_menu: request.start_menu,
        desktop: request.desktop,
        add_to_path: request.add_to_path,
        associate: request.associate,
    };
    let result = install_self(&entries, &options, &Env::system(placement.scope)?);
    if let (Err(message), Some(file)) = (&result, &request.result_file) {
        let _ = fs::write(file, message);
    }
    result.map(|_| dir)
}

/// What `run_uninstall` did.
#[derive(Debug, PartialEq, Eq)]
pub enum Uninstalled {
    /// Finished here, with notes about anything left behind.
    Done(Vec<String>),
    /// Handed to another copy of the program (a temporary copy, or an elevated one) that does the work.
    Handed,
}

const RELOCATED_PREFIX: &str = "crossport-uninstall-";

fn is_relocated(exe: &Path) -> bool {
    exe.file_name().is_some_and(|f| f.to_string_lossy().starts_with(RELOCATED_PREFIX))
}

/// Uninstall the install in `dir`. A program cannot delete its own file, so when this is the uninstaller
/// inside `dir` it starts a temporary copy and leaves; an install for all users is removed by an elevated copy.
pub fn run_uninstall(dir: &Path, exe: &Path, quiet: bool) -> Result<Uninstalled, String> {
    let mut args = vec!["--uninstall".to_string(), dir.to_string_lossy().into_owned(), "--quiet".to_string()];
    if crate::paths::is_inside(exe, dir) && !is_relocated(exe) {
        let copy = std::env::temp_dir().join(format!("{RELOCATED_PREFIX}{}.exe", std::process::id()));
        fs::copy(exe, &copy).map_err(|e| format!("preparing the uninstaller: {e}"))?;
        if !quiet {
            args.pop();
        }
        std::process::Command::new(&copy).args(&args).spawn().map_err(|e| e.to_string())?;
        return Ok(Uninstalled::Handed);
    }
    let text = fs::read_to_string(dir.join(crate::manifest::MANIFEST_FILE)).map_err(|e| format!("{}: {e}", dir.display()))?;
    let manifest = crate::manifest::Manifest::parse(&text).map_err(|e| format!("{e:?}"))?;
    if manifest.scope == Scope::Machine && !is_elevated() {
        let code = run_elevated(exe, &args)?;
        return if code == 0 { Ok(Uninstalled::Handed) } else { Err("the uninstall did not finish".into()) };
    }
    let notes = crate::engine::uninstall::uninstall(dir, &Env::system(manifest.scope)?)?;
    if is_relocated(exe) {
        delete_self_later(exe);
    }
    Ok(Uninstalled::Done(notes))
}

/// Delete this running program's file a moment after it exits, through a short-lived `cmd`.
fn delete_self_later(exe: &Path) {
    let command = format!("ping 127.0.0.1 -n 3 >nul & del /f /q \"{}\"", exe.display());
    let _ = std::process::Command::new("cmd").args(["/c", &command]).spawn();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn names_prefer_the_user_then_the_desktop_entry_then_the_file() {
        let image = Path::new("C:\\dl\\KeePassXC-2.8.0-x86_64.AppImage");
        assert_eq!(display_name(Some("Mine"), Some("Desk"), image), "Mine");
        assert_eq!(display_name(None, Some("Desk"), image), "Desk");
        assert_eq!(display_name(None, None, image), "KeePassXC");
        assert_eq!(display_name(Some("  "), None, image), "KeePassXC");
    }

    #[test]
    fn result_files_round_trip() {
        let file = std::env::temp_dir().join(format!("crossport-ops-{}.txt", std::process::id()));
        let report = Report { succeeded: true, findings: vec![] };
        write_result(&file, Some(Path::new("C:\\a\\b.exe")), &report);
        assert_eq!(read_result(&file), Ok((Some(PathBuf::from("C:\\a\\b.exe")), report.clone())));
        write_result(&file, None, &report);
        assert_eq!(read_result(&file), Ok((None, report)));
        let _ = fs::remove_file(&file);
        assert!(read_result(&file).is_err());
    }
}
