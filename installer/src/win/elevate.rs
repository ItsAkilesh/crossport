// File: installer/src/win/elevate.rs
// Purpose: Start a program with administrator rights (UAC prompt) and wait for it to finish
// Author: Akilesh
// Date: 2026-11-05

#![allow(unsafe_code)]

use std::path::Path;

use windows::core::PCWSTR;
use windows::Win32::Foundation::{CloseHandle, WAIT_OBJECT_0};
use windows::Win32::System::Threading::{GetExitCodeProcess, WaitForSingleObject, INFINITE};
use windows::Win32::UI::Shell::{ShellExecuteExW, SEE_MASK_NOCLOSEPROCESS, SHELLEXECUTEINFOW};
use windows::Win32::UI::WindowsAndMessaging::SW_HIDE;

use super::{pcwstr, wide};
use crate::command::quote_arg;

/// Run `program` with `args` elevated and return its exit code. The user can decline the prompt, which is an error.
pub fn run_elevated(program: &Path, args: &[String]) -> Result<u32, String> {
    let line: Vec<String> = args.iter().map(|a| quote_arg(a)).collect();
    let (verb, file, params) = (wide("runas"), wide(&program.to_string_lossy()), wide(&line.join(" ")));
    let mut info = SHELLEXECUTEINFOW {
        cbSize: std::mem::size_of::<SHELLEXECUTEINFOW>() as u32,
        fMask: SEE_MASK_NOCLOSEPROCESS,
        lpVerb: pcwstr(&verb),
        lpFile: pcwstr(&file),
        lpParameters: pcwstr(&params),
        lpDirectory: PCWSTR::null(),
        nShow: SW_HIDE.0,
        ..Default::default()
    };
    // SAFETY: the buffers referenced by `info` outlive the call; the process handle is closed after the wait.
    unsafe {
        ShellExecuteExW(&mut info).map_err(|_| "administrator permission was not given".to_string())?;
        let process = info.hProcess;
        let waited = WaitForSingleObject(process, INFINITE);
        let mut code = 1u32;
        let got = GetExitCodeProcess(process, &mut code);
        let _ = CloseHandle(process);
        if waited != WAIT_OBJECT_0 || got.is_err() {
            return Err("lost track of the administrator process".to_string());
        }
        Ok(code)
    }
}
