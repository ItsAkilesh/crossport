// File: installer/src/engine/app.rs
// Purpose: Install one AppImage as a Windows app: convert it, register it, make shortcuts, write the manifest
// Author: Akilesh
// Date: 2026-11-05

use std::fs;
use std::os::windows::process::CommandExt;
use std::path::{Path, PathBuf};
use std::process::Command;

use super::{folder_size_kb, Env, Tools, UNINSTALLER};
use crate::command::{quote_arg, ConvertJob};
use crate::ico::png_to_ico;
use crate::manifest::{Manifest, MANIFEST_FILE};
use crate::names::folder_name;
use crate::paths::Scope;
use crate::regplan::{uninstall_entries, uninstall_key, Hive, UninstallInfo};
use crate::report::{analyze, Report};
use crate::win::shortcut::{create, Link};

const CREATE_NO_WINDOW: u32 = 0x0800_0000;
const ICON_FILE: &str = "crossport-icon.ico";
const WROTE_MARK: &str = "wrote ";
const APP_VERSION: &str = "1.0";

/// What to install and how.
pub struct AppRequest<'a> {
    pub image: &'a Path,
    pub name: &'a str,
    pub dir: &'a Path,
    pub scope: Scope,
    pub start_menu: bool,
    pub desktop: bool,
    pub stub_missing: bool,
    pub icon_png: Option<&'a [u8]>,
    /// The launcher program, copied into the app folder as its uninstaller.
    pub launcher: &'a Path,
}

/// The outcome: what the conversion said and, when it worked, the program to run.
#[derive(Debug)]
pub struct AppOutcome {
    pub report: Report,
    pub exe: Option<PathBuf>,
}

/// The program a conversion wrote, from its `wrote <path> (N modules)` line.
pub fn parse_exe(output: &str) -> Option<PathBuf> {
    output.lines().find_map(|line| {
        let rest = line.trim().strip_prefix(WROTE_MARK)?;
        let path = rest.rsplit_once(" (")?.0;
        path.to_ascii_lowercase().ends_with(".exe").then(|| PathBuf::from(path))
    })
}

fn convert(req: &AppRequest, tools: &Tools) -> Result<(Report, String), String> {
    let job = ConvertJob {
        image: req.image,
        shim: &tools.shim,
        out: req.dir,
        runtime: tools.runtime.as_deref(),
        stub_missing: req.stub_missing,
    };
    let output = Command::new(&tools.translator)
        .args(job.args())
        .creation_flags(CREATE_NO_WINDOW)
        .output()
        .map_err(|e| format!("could not start {}: {e}", tools.translator.display()))?;
    let text = format!("{}{}", String::from_utf8_lossy(&output.stdout), String::from_utf8_lossy(&output.stderr));
    Ok((analyze(&text, output.status.success()), text))
}

/// Convert and register an AppImage. On a failed conversion the folder is removed again and the report says why.
pub fn install_app(req: &AppRequest, tools: &Tools, env: &Env) -> Result<AppOutcome, String> {
    if let Some(problem) = tools.problem() {
        return Err(problem);
    }
    if req.dir.exists() && fs::read_dir(req.dir).map_err(|e| e.to_string())?.next().is_some() {
        return Err(format!("{} already exists and is not empty", req.dir.display()));
    }
    let existed = req.dir.exists();
    let (report, text) = convert(req, tools)?;
    let exe = parse_exe(&text).filter(|_| report.succeeded);
    let Some(exe) = exe else {
        if !existed {
            let _ = fs::remove_dir_all(req.dir);
        } else {
            let _ = fs::remove_dir_all(req.dir).and_then(|_| fs::create_dir(req.dir));
        }
        return Ok(AppOutcome { report, exe: None });
    };
    register(req, &exe, env).map_err(|e| {
        let _ = fs::remove_dir_all(req.dir);
        e
    })?;
    Ok(AppOutcome { report, exe: Some(exe) })
}

fn register(req: &AppRequest, exe: &Path, env: &Env) -> Result<(), String> {
    let id_name = folder_name(req.name).map_err(|_| "the app name is empty".to_string())?;
    let uninstaller = req.dir.join(UNINSTALLER);
    fs::copy(req.launcher, &uninstaller).map_err(|e| format!("copying the uninstaller: {e}"))?;
    let icon = req.icon_png.and_then(png_to_ico).and_then(|ico| {
        let path = req.dir.join(ICON_FILE);
        fs::write(&path, ico).ok().map(|_| path)
    });
    let icon_ref = icon.as_deref().unwrap_or(exe);
    let mut manifest = Manifest::new(req.name, req.scope, req.dir.to_path_buf());
    let link = Link { target: exe, arguments: "", working_dir: req.dir, description: req.name, icon: Some(icon_ref) };
    for (wanted, folder) in [(req.start_menu, &env.start_menu), (req.desktop, &env.desktop)] {
        if wanted {
            let path = folder.join(format!("{id_name}.lnk"));
            create(&path, &link)?;
            manifest.shortcuts.push(path);
        }
    }
    let id = format!("Crossport.App.{id_name}");
    let command = format!("{} --uninstall {}", quote_arg(&uninstaller.to_string_lossy()), quote_arg(&req.dir.to_string_lossy()));
    let info = UninstallInfo {
        id: &id,
        display_name: req.name,
        version: APP_VERSION,
        install_dir: req.dir,
        uninstall_command: &command,
        icon: Some(icon_ref),
        size_kb: folder_size_kb(req.dir),
    };
    env.registry.apply(&uninstall_entries(req.scope, &info))?;
    manifest.keys.push((Hive::for_scope(req.scope).name().to_string(), uninstall_key(&id)));
    let text = manifest.serialize().map_err(|e| format!("{e:?}"))?;
    fs::write(req.dir.join(MANIFEST_FILE), text).map_err(|e| e.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn finds_the_program_in_conversion_output() {
        let text = "program: usr/bin/lua\nwrote C:/Apps/Lua/lua.exe (3 modules)\nwrote C:/Apps/Lua/crossport.env\n";
        assert_eq!(parse_exe(text), Some(PathBuf::from("C:/Apps/Lua/lua.exe")));
        assert_eq!(parse_exe("program: x\n"), None);
    }

    #[test]
    fn paths_with_parentheses_still_parse() {
        let text = "wrote C:/Program Files (x86)/A/a.exe (2 modules)\n";
        assert_eq!(parse_exe(text), Some(PathBuf::from("C:/Program Files (x86)/A/a.exe")));
    }
}
