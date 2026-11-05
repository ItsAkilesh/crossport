// File: installer/src/bin/open.rs
// Purpose: crossport-open.exe: the launcher that installs an .AppImage as a Windows app (wizard or --silent)
// Author: Akilesh
// Date: 2026-11-05

#![cfg_attr(windows, windows_subsystem = "windows")]

#[cfg(windows)]
fn main() {
    use crossport_setup::cli::{parse_open, OpenCommand};
    use crossport_setup::engine::Tools;
    use crossport_setup::flows::{open::OpenFlow, uninstall};
    use crossport_setup::ops::run_open;
    use crossport_setup::ui::{dialogs, window};

    const EXIT_USAGE: i32 = 64;
    const EXIT_UNSUPPORTED: i32 = 2;

    crossport_setup::win::attach_console();
    let args: Vec<String> = std::env::args().skip(1).collect();
    let exe = std::env::current_exe().unwrap_or_default();
    let dir = exe.parent().map(std::path::Path::to_path_buf).unwrap_or_default();
    let tools = Tools::beside(&dir);
    let code = match parse_open(&args) {
        Err(message) => {
            dialogs::error(None, &format!("{message}\n\nUsage: crossport-open [file.AppImage] [--silent --dir <folder> --user --desktop]"));
            EXIT_USAGE
        }
        Ok(OpenCommand::Uninstall { dir, quiet }) => uninstall::run(&dir, &exe, quiet),
        Ok(OpenCommand::Silent(request)) => match run_open(&request, &exe, &tools) {
            Ok(done) => {
                for finding in &done.report.findings {
                    eprintln!("{}\n    {}", finding.summary, finding.advice);
                }
                if let Some(program) = &done.exe {
                    println!("installed {}", program.display());
                }
                if done.exe.is_some() { 0 } else { EXIT_UNSUPPORTED }
            }
            Err(message) => {
                eprintln!("crossport: {message}");
                1
            }
        },
        Ok(OpenCommand::Wizard { image }) => match tools.problem() {
            Some(problem) => {
                dialogs::error(None, &problem);
                1
            }
            None => {
                window::run(Box::new(OpenFlow::new(image, exe, tools)), "Crossport");
                0
            }
        },
    };
    std::process::exit(code);
}

#[cfg(not(windows))]
fn main() {
    eprintln!("crossport-open runs on Windows only");
    std::process::exit(1);
}
