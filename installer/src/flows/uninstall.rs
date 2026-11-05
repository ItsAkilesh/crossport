// File: installer/src/flows/uninstall.rs
// Purpose: The uninstall command used by Apps and features: confirm, remove, report
// Author: Akilesh
// Date: 2026-11-05

use std::path::Path;

use crate::ops::{run_uninstall, Uninstalled};
use crate::ui::dialogs;

pub const EXIT_OK: i32 = 0;
pub const EXIT_FAILED: i32 = 1;

/// Uninstall the install in `dir`. Unless `quiet`, ask first and report afterwards.
pub fn run(dir: &Path, exe: &Path, quiet: bool) -> i32 {
    if !quiet && !dialogs::confirm(None, &format!("Remove the program in {} and everything Crossport set up for it?", dir.display())) {
        return EXIT_OK;
    }
    match run_uninstall(dir, exe, quiet) {
        Ok(Uninstalled::Handed) => EXIT_OK,
        Ok(Uninstalled::Done(notes)) => {
            if !quiet {
                let message = if notes.is_empty() { "Removed.".to_string() } else { format!("Removed, with notes:\n{}", notes.join("\n")) };
                dialogs::info(None, &message);
            }
            EXIT_OK
        }
        Err(message) => {
            if !quiet {
                dialogs::error(None, &message);
            }
            eprintln!("crossport: {message}");
            EXIT_FAILED
        }
    }
}
