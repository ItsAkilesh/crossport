// File: installer/src/ui/dialogs.rs
// Purpose: Folder and file pickers and message boxes
// Author: Akilesh
// Date: 2026-11-05

#![allow(unsafe_code)]

use std::path::PathBuf;

use windows::core::{w, PCWSTR};
use windows::Win32::Foundation::HWND;
use windows::Win32::System::Com::{CoCreateInstance, CoInitializeEx, CoTaskMemFree, CLSCTX_INPROC_SERVER, COINIT_APARTMENTTHREADED};
use windows::Win32::UI::Shell::Common::COMDLG_FILTERSPEC;
use windows::Win32::UI::Shell::{FileOpenDialog, IFileOpenDialog, FOS_PICKFOLDERS, SIGDN_FILESYSPATH};
use windows::Win32::UI::WindowsAndMessaging::{
    MessageBoxW, IDYES, MB_ICONERROR, MB_ICONINFORMATION, MB_ICONQUESTION, MB_OK, MB_YESNO,
};

use super::Browse;
use crate::win::{pcwstr, wide};

/// Ask for a folder or an AppImage. `None` when the user cancels.
pub fn pick(owner: HWND, browse: Browse, title: &str) -> Option<PathBuf> {
    // SAFETY: COM is initialised on this thread; the dialog and its result are released when dropped, and the
    // returned name buffer is freed after it is copied.
    unsafe {
        let _ = CoInitializeEx(None, COINIT_APARTMENTTHREADED);
        let dialog: IFileOpenDialog = CoCreateInstance(&FileOpenDialog, None, CLSCTX_INPROC_SERVER).ok()?;
        let title = wide(title);
        let _ = dialog.SetTitle(pcwstr(&title));
        if browse == Browse::Folder {
            let options = dialog.GetOptions().ok()?;
            dialog.SetOptions(options | FOS_PICKFOLDERS).ok()?;
        } else {
            let filters = [
                COMDLG_FILTERSPEC { pszName: w!("AppImage files"), pszSpec: w!("*.AppImage") },
                COMDLG_FILTERSPEC { pszName: w!("All files"), pszSpec: w!("*.*") },
            ];
            dialog.SetFileTypes(&filters).ok()?;
        }
        dialog.Show(Some(owner)).ok()?;
        let name = dialog.GetResult().ok()?.GetDisplayName(SIGDN_FILESYSPATH).ok()?;
        let path = name.to_string().ok().map(PathBuf::from);
        CoTaskMemFree(Some(name.0 as *const _));
        path
    }
}

fn show(owner: Option<HWND>, text: &str, caption: &str, style: windows::Win32::UI::WindowsAndMessaging::MESSAGEBOX_STYLE) -> bool {
    let (text, caption) = (wide(text), wide(caption));
    // SAFETY: both strings are NUL-terminated and outlive the call.
    unsafe { MessageBoxW(owner, pcwstr(&text), PCWSTR(caption.as_ptr()), style) == IDYES }
}

pub fn error(owner: Option<HWND>, text: &str) {
    show(owner, text, "Crossport", MB_OK | MB_ICONERROR);
}

pub fn info(owner: Option<HWND>, text: &str) {
    show(owner, text, "Crossport", MB_OK | MB_ICONINFORMATION);
}

pub fn confirm(owner: Option<HWND>, text: &str) -> bool {
    show(owner, text, "Crossport", MB_YESNO | MB_ICONQUESTION)
}
