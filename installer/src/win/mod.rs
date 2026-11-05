// File: installer/src/win/mod.rs
// Purpose: Windows-only helpers: wide strings, process elevation, known folders
// Author: Akilesh
// Date: 2026-11-05

#![allow(unsafe_code)]

pub mod registry;
pub mod shortcut;

use std::path::PathBuf;

use windows::core::{PCWSTR, PWSTR};
use windows::Win32::Foundation::{CloseHandle, HANDLE};
use windows::Win32::Security::{GetTokenInformation, TokenElevation, TOKEN_ELEVATION, TOKEN_QUERY};
use windows::Win32::System::Com::CoTaskMemFree;
use windows::Win32::System::Threading::{GetCurrentProcess, OpenProcessToken};
use windows::Win32::UI::Shell::{
    FOLDERID_LocalAppData, FOLDERID_ProgramFiles, SHGetKnownFolderPath, KF_FLAG_DEFAULT,
};

use crate::paths::Roots;

/// A NUL-terminated UTF-16 copy of `text`.
pub fn wide(text: &str) -> Vec<u16> {
    text.encode_utf16().chain(std::iter::once(0)).collect()
}

/// Borrow a wide string as a `PCWSTR`. The slice must outlive the call.
pub fn pcwstr(buffer: &[u16]) -> PCWSTR {
    PCWSTR(buffer.as_ptr())
}

/// True when this process runs with administrator rights.
pub fn is_elevated() -> bool {
    let mut token = HANDLE::default();
    // SAFETY: the process handle is a pseudo handle and `token` is a valid out parameter.
    unsafe {
        if OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut token).is_err() {
            return false;
        }
        let mut elevation = TOKEN_ELEVATION::default();
        let mut returned = 0u32;
        let size = std::mem::size_of::<TOKEN_ELEVATION>() as u32;
        let result = GetTokenInformation(
            token,
            TokenElevation,
            Some(&mut elevation as *mut TOKEN_ELEVATION as *mut _),
            size,
            &mut returned,
        );
        let _ = CloseHandle(token);
        result.is_ok() && elevation.TokenIsElevated != 0
    }
}

pub(crate) fn known_folder(id: &windows::core::GUID) -> Option<PathBuf> {
    // SAFETY: the returned buffer is freed with CoTaskMemFree after it is copied.
    unsafe {
        let raw: PWSTR = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, None).ok()?;
        let text = raw.to_string().ok();
        CoTaskMemFree(Some(raw.0 as *const _));
        text.map(PathBuf::from)
    }
}

/// The install roots of this machine and user.
pub fn roots() -> Option<Roots> {
    Some(Roots {
        program_files: known_folder(&FOLDERID_ProgramFiles)?,
        local_app_data: known_folder(&FOLDERID_LocalAppData)?,
    })
}

/// True when the current user can create a file in `dir`, or in the nearest folder above it that exists.
pub fn can_write(dir: &std::path::Path) -> bool {
    let mut probe = dir;
    while !probe.is_dir() {
        match probe.parent() {
            Some(parent) => probe = parent,
            None => return false,
        }
    }
    let file = probe.join(format!(".crossport-probe-{}", std::process::id()));
    let ok = std::fs::write(&file, b"").is_ok();
    let _ = std::fs::remove_file(&file);
    ok
}
