// File: installer/src/flows/update.rs
// Purpose: The --check-update command: ask GitHub once, tell the user, and offer the release page
// Author: Akilesh
// Date: 2026-11-05

use crate::ui::dialogs;
use crate::update::{check, Outcome, Release, API_HOST, VERSION};
use crate::win::{http, open_release_page};

pub const EXIT_UP_TO_DATE: i32 = 0;
pub const EXIT_FAILED: i32 = 1;
/// Quiet mode answers a script with this code when a newer release exists.
pub const EXIT_UPDATE_AVAILABLE: i32 = 10;
const MAX_ANSWER_BYTES: usize = 512 * 1024;

fn offer(release: &Release) -> String {
    let have = format!(
        "Crossport {} is available (you have {VERSION}).",
        release.version
    );
    format!("{have}\n\nOpen the download page in your browser? The page lists the setup file and its SHA-256 checksum.")
}

/// Check for a newer release. Unless `quiet`, show the answer and offer the download page.
pub fn run(quiet: bool) -> i32 {
    match check(VERSION, |url| {
        http::get_text(url, API_HOST, MAX_ANSWER_BYTES)
    }) {
        Ok(Outcome::UpToDate) => {
            if quiet {
                println!("Crossport {VERSION} is up to date");
            } else {
                dialogs::info(None, &format!("Crossport {VERSION} is up to date."));
            }
            EXIT_UP_TO_DATE
        }
        Ok(Outcome::Available(release)) => {
            println!(
                "Crossport {} is available: {}",
                release.version, release.page
            );
            if !quiet && dialogs::confirm(None, &offer(&release)) {
                if let Err(message) = open_release_page(&release.page) {
                    dialogs::error(None, &message);
                    return EXIT_FAILED;
                }
            }
            EXIT_UPDATE_AVAILABLE
        }
        Err(message) => {
            eprintln!("crossport: update check failed: {message}");
            if !quiet {
                dialogs::error(None, &format!("Could not check for updates.\n\n{message}"));
            }
            EXIT_FAILED
        }
    }
}
