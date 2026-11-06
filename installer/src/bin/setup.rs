// File: installer/src/bin/setup.rs
// Purpose: crossport-setup.exe: installs Crossport (wizard or --silent), uninstalls it, and packs setup files into itself
// Author: Akilesh
// Date: 2026-11-05

#![cfg_attr(windows, windows_subsystem = "windows")]

#[cfg(windows)]
fn make_installer(out: &std::path::Path, from: &std::path::Path, uninstaller: Option<&std::path::Path>) -> Result<(), String> {
    use crossport_setup::engine::UNINSTALLER;
    use crossport_setup::payload::{attach, collect, encode, Entry};

    let exe = std::env::current_exe().map_err(|e| e.to_string())?;
    let program = std::fs::read(&exe).map_err(|e| format!("reading {}: {e}", exe.display()))?;
    let mut entries = collect(from).map_err(|e| format!("reading {}: {e}", from.display()))?;
    let uninstall_exe = match uninstaller {
        Some(path) => std::fs::read(path).map_err(|e| format!("reading {}: {e}", path.display()))?,
        None => program.clone(),
    };
    entries.push(Entry { path: UNINSTALLER.to_string(), data: uninstall_exe });
    std::fs::write(out, attach(&program, &encode(&entries))).map_err(|e| format!("writing {}: {e}", out.display()))
}

#[cfg(windows)]
fn main() {
    use crossport_setup::cli::{parse_setup, SetupCommand};
    use crossport_setup::flows::{setup::SetupFlow, uninstall};
    use crossport_setup::ops::run_setup;
    use crossport_setup::ui::{dialogs, window};

    const EXIT_USAGE: i32 = 64;

    crossport_setup::win::attach_console();
    let args: Vec<String> = std::env::args().skip(1).collect();
    let exe = std::env::current_exe().unwrap_or_default();
    let code = match parse_setup(&args) {
        Err(message) => {
            dialogs::error(None, &message);
            EXIT_USAGE
        }
        Ok(SetupCommand::Uninstall { dir, quiet }) => uninstall::run(&dir, &exe, quiet),
        Ok(SetupCommand::CheckUpdate { quiet }) => crossport_setup::flows::update::run(quiet),
        Ok(SetupCommand::MakeInstaller { out, from, uninstaller }) => match make_installer(&out, &from, uninstaller.as_deref()) {
            Ok(()) => 0,
            Err(message) => {
                eprintln!("crossport-setup: {message}");
                1
            }
        },
        Ok(SetupCommand::Silent(request)) => match run_setup(&request, &exe) {
            Ok(dir) => {
                println!("installed Crossport in {}", dir.display());
                0
            }
            Err(message) => {
                eprintln!("crossport-setup: {message}");
                1
            }
        },
        Ok(SetupCommand::Wizard) => {
            window::run(Box::new(SetupFlow::new(exe)), "Crossport Setup");
            0
        }
    };
    std::process::exit(code);
}

#[cfg(not(windows))]
fn main() {
    eprintln!("crossport-setup runs on Windows only");
    std::process::exit(1);
}
