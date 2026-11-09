// File: bus/src/main.rs
// Purpose: The crossport-bus command line: start the session bus, print its address, and serve until idle
// Author: Akilesh
// Date: 2026-11-09

use std::io::Write;
use std::path::PathBuf;
use std::process::ExitCode;
use std::time::Duration;

use crossport_bus::server::{run, Config};

const USAGE: &str = "usage: crossport-bus --nonce-file <path> [--advertise-nonce-file <path as clients see it>]\n\
                     \x20                    [--address-file <path>] [--idle-exit <seconds>] [--verbose]\n\
Prints the bus address on its first line of standard output and serves until no client has been connected for the idle time.";

fn parse(arguments: &[String]) -> Result<Config, String> {
    let mut nonce_file = None;
    let mut config = Config {
        nonce_file: PathBuf::new(),
        advertised_nonce_file: None,
        address_file: None,
        idle_exit: None,
        verbose: false,
    };
    let mut remaining = arguments.iter();
    while let Some(flag) = remaining.next() {
        if flag == "--verbose" {
            config.verbose = true;
            continue;
        }
        let value = remaining
            .next()
            .ok_or_else(|| format!("{flag} needs a value"))?;
        match flag.as_str() {
            "--nonce-file" => nonce_file = Some(PathBuf::from(value)),
            "--advertise-nonce-file" => config.advertised_nonce_file = Some(value.clone()),
            "--address-file" => config.address_file = Some(PathBuf::from(value)),
            "--idle-exit" => {
                let seconds: u64 = value
                    .parse()
                    .map_err(|_| format!("{value} is not a number of seconds"))?;
                config.idle_exit = Some(Duration::from_secs(seconds));
            }
            _ => return Err(format!("unknown option {flag}")),
        }
    }
    config.nonce_file = nonce_file.ok_or("--nonce-file is required")?;
    Ok(config)
}

fn main() -> ExitCode {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    let config = match parse(&arguments) {
        Ok(config) => config,
        Err(message) => {
            eprintln!("crossport-bus: {message}\n{USAGE}");
            return ExitCode::from(2);
        }
    };
    let announce = |address: &str| {
        let mut out = std::io::stdout();
        let _ = writeln!(out, "{address}");
        let _ = out.flush();
    };
    match run(&config, announce) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("crossport-bus: {error}");
            ExitCode::FAILURE
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args(items: &[&str]) -> Vec<String> {
        items.iter().map(|item| (*item).to_string()).collect()
    }

    #[test]
    fn the_options_are_read() {
        let config = parse(&args(&[
            "--nonce-file",
            "n",
            "--idle-exit",
            "30",
            "--verbose",
            "--address-file",
            "a",
        ]))
        .expect("ok");
        assert_eq!(config.nonce_file, PathBuf::from("n"));
        assert_eq!(config.idle_exit, Some(Duration::from_secs(30)));
        assert!(config.verbose);
    }

    #[test]
    fn a_missing_nonce_file_or_a_bad_option_is_an_error() {
        assert!(parse(&args(&[])).is_err());
        assert!(parse(&args(&["--nonce-file"])).is_err());
        assert!(parse(&args(&["--nonce-file", "n", "--bogus", "x"])).is_err());
        assert!(parse(&args(&["--nonce-file", "n", "--idle-exit", "soon"])).is_err());
    }
}
