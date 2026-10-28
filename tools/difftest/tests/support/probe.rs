// File: tools/difftest/tests/support/probe.rs
// Purpose: Test support program: reports its arguments, environment, stdin and directory, or writes files and exits on request
// Author: Akilesh
// Date: 2026-10-28

use std::io::{Read, Write};
use std::process::ExitCode;
use std::time::Duration;

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let Some(command) = args.first() else {
        return ExitCode::from(64);
    };
    let rest = &args[1..];
    match command.as_str() {
        "args" => rest.iter().for_each(|arg| println!("{arg}")),
        "env" => {
            for name in rest {
                println!(
                    "{}",
                    std::env::var(name).unwrap_or_else(|_| "<unset>".into())
                );
            }
        }
        "stdin" => {
            let mut input = Vec::new();
            let _ = std::io::stdin().read_to_end(&mut input);
            let _ = std::io::stdout().write_all(&input);
        }
        "cwd" => println!(
            "{}",
            std::env::current_dir()
                .map(|p| p.display().to_string())
                .unwrap_or_default()
        ),
        "sleep" => std::thread::sleep(Duration::from_millis(
            rest.first().and_then(|t| t.parse().ok()).unwrap_or(0),
        )),
        "write" => {
            if let [path, text] = rest {
                let _ = std::fs::write(path, text);
            }
        }
        "stderr" => eprintln!("{}", rest.join(" ")),
        "flood" => {
            let line = "x".repeat(1023) + "\n";
            for _ in 0..rest.first().and_then(|n| n.parse().ok()).unwrap_or(0) {
                print!("{line}");
            }
        }
        "exit" => return ExitCode::from(rest.first().and_then(|n| n.parse().ok()).unwrap_or(0)),
        _ => return ExitCode::from(65),
    }
    ExitCode::SUCCESS
}
