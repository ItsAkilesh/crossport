// File: installer/src/win/shortcut.rs
// Purpose: Create .lnk shortcuts and find the Start menu and desktop folders for a scope
// Author: Akilesh
// Date: 2026-11-05

#![allow(unsafe_code)]

use std::path::{Path, PathBuf};

use windows::core::Interface;
use windows::Win32::System::Com::{
    CoCreateInstance, CoInitializeEx, IPersistFile, CLSCTX_INPROC_SERVER, COINIT_APARTMENTTHREADED,
};
use windows::Win32::UI::Shell::{
    FOLDERID_CommonPrograms, FOLDERID_Desktop, FOLDERID_Programs, FOLDERID_PublicDesktop, IShellLinkW, ShellLink,
};

use super::{known_folder, pcwstr, wide};
use crate::paths::Scope;

/// Where a shortcut goes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Place {
    StartMenu,
    Desktop,
}

/// The folder for a kind of shortcut: per user, or shared by all users for a machine install.
pub fn folder(scope: Scope, place: Place) -> Option<PathBuf> {
    let id = match (scope, place) {
        (Scope::User, Place::StartMenu) => &FOLDERID_Programs,
        (Scope::Machine, Place::StartMenu) => &FOLDERID_CommonPrograms,
        (Scope::User, Place::Desktop) => &FOLDERID_Desktop,
        (Scope::Machine, Place::Desktop) => &FOLDERID_PublicDesktop,
    };
    known_folder(id)
}

/// What a shortcut points at.
pub struct Link<'a> {
    pub target: &'a Path,
    pub arguments: &'a str,
    pub working_dir: &'a Path,
    pub description: &'a str,
    pub icon: Option<&'a Path>,
}

fn failed_at(what: &'static str) -> impl Fn(windows::core::Error) -> String {
    move |e| format!("{what}: {e}")
}

/// Write a shortcut file at `path`, replacing any existing one.
pub fn create(path: &Path, link: &Link) -> Result<(), String> {
    // SAFETY: COM is initialised for this thread (a repeat call is harmless); every string buffer outlives
    // the call that reads it.
    unsafe {
        let _ = CoInitializeEx(None, COINIT_APARTMENTTHREADED);
        let shell: IShellLinkW = CoCreateInstance(&ShellLink, None, CLSCTX_INPROC_SERVER)
            .map_err(|e| format!("shell link: {e}"))?;
        let strings = [
            wide(&link.target.to_string_lossy()),
            wide(link.arguments),
            wide(&link.working_dir.to_string_lossy()),
            wide(link.description),
        ];
        shell.SetPath(pcwstr(&strings[0])).map_err(failed_at("path"))?;
        shell.SetArguments(pcwstr(&strings[1])).map_err(failed_at("arguments"))?;
        shell.SetWorkingDirectory(pcwstr(&strings[2])).map_err(failed_at("directory"))?;
        shell.SetDescription(pcwstr(&strings[3])).map_err(failed_at("description"))?;
        if let Some(icon) = link.icon {
            let icon = wide(&icon.to_string_lossy());
            shell.SetIconLocation(pcwstr(&icon), 0).map_err(failed_at("icon"))?;
        }
        let file: IPersistFile = shell.cast().map_err(failed_at("persist"))?;
        let target = wide(&path.to_string_lossy());
        file.Save(pcwstr(&target), true).map_err(failed_at("save"))
    }
}

/// Read back the target of a shortcut (used by tests and by the uninstaller's sanity checks).
pub fn target_of(path: &Path) -> Option<String> {
    // SAFETY: as in `create`; the path buffer is filled by the shell within its stated length.
    unsafe {
        let _ = CoInitializeEx(None, COINIT_APARTMENTTHREADED);
        let shell: IShellLinkW = CoCreateInstance(&ShellLink, None, CLSCTX_INPROC_SERVER).ok()?;
        let file: IPersistFile = shell.cast().ok()?;
        let source = wide(&path.to_string_lossy());
        file.Load(pcwstr(&source), windows::Win32::System::Com::STGM(0)).ok()?;
        let mut buffer = [0u16; 520];
        shell.GetPath(&mut buffer, std::ptr::null_mut(), 0).ok()?;
        let end = buffer.iter().position(|&c| c == 0).unwrap_or(buffer.len());
        Some(String::from_utf16_lossy(&buffer[..end]))
    }
}
