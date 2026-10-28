// File: translator/src/cli.rs
// Purpose: Parse command-line arguments into a typed Action
// Author: Akilesh
// Date: 2026-10-28

use std::path::PathBuf;

use crate::error::TranslateError;

const USAGE: &str = "usage:\n  crossport analyze <elf>\n  crossport translate <elf> --shim <crossport_libc.dll> --out <dir> [--lib-dir <dir>]...";

#[derive(Debug, PartialEq, Eq)]
pub enum Action {
    Analyze {
        elf: PathBuf,
    },
    Translate {
        elf: PathBuf,
        shim: PathBuf,
        out: PathBuf,
        lib_dirs: Vec<PathBuf>,
    },
}

pub fn parse(args: &[String]) -> Result<Action, TranslateError> {
    match args {
        [command, elf] if command == "analyze" => Ok(Action::Analyze {
            elf: PathBuf::from(elf),
        }),
        [command, elf, flags @ ..] if command == "translate" => parse_translate(elf, flags),
        _ => Err(usage()),
    }
}

fn parse_translate(elf: &str, flags: &[String]) -> Result<Action, TranslateError> {
    let mut shim = None;
    let mut out = None;
    let mut lib_dirs = Vec::new();
    let mut remaining = flags.iter();
    while let Some(flag) = remaining.next() {
        let value = PathBuf::from(remaining.next().ok_or_else(usage)?);
        match flag.as_str() {
            "--shim" if shim.is_none() => shim = Some(value),
            "--out" if out.is_none() => out = Some(value),
            "--lib-dir" => lib_dirs.push(value),
            _ => return Err(usage()),
        }
    }
    match (shim, out) {
        (Some(shim), Some(out)) => Ok(Action::Translate {
            elf: PathBuf::from(elf),
            shim,
            out,
            lib_dirs,
        }),
        _ => Err(usage()),
    }
}

fn usage() -> TranslateError {
    TranslateError::Usage(USAGE.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args(items: &[&str]) -> Vec<String> {
        items.iter().map(|item| item.to_string()).collect()
    }

    #[test]
    fn parses_analyze() {
        assert_eq!(
            parse(&args(&["analyze", "a.out"])).expect("valid"),
            Action::Analyze {
                elf: PathBuf::from("a.out")
            }
        );
    }

    #[test]
    fn parses_translate_with_repeated_library_directories_in_any_order() {
        let action = parse(&args(&[
            "translate",
            "a.out",
            "--lib-dir",
            "l1",
            "--out",
            "dist",
            "--shim",
            "s.dll",
            "--lib-dir",
            "l2",
        ]));
        assert_eq!(
            action.expect("valid"),
            Action::Translate {
                elf: PathBuf::from("a.out"),
                shim: PathBuf::from("s.dll"),
                out: PathBuf::from("dist"),
                lib_dirs: vec![PathBuf::from("l1"), PathBuf::from("l2")],
            }
        );
    }

    #[test]
    fn rejects_incomplete_duplicate_or_unknown_flags() {
        assert!(parse(&[]).is_err());
        assert!(parse(&args(&["translate", "a.out"])).is_err());
        assert!(parse(&args(&["translate", "a.out", "--shim", "s", "--out"])).is_err());
        assert!(parse(&args(&[
            "translate",
            "a.out",
            "--shim",
            "s",
            "--shim",
            "t",
            "--out",
            "d"
        ]))
        .is_err());
        assert!(parse(&args(&[
            "translate",
            "a.out",
            "--shim",
            "s",
            "--out",
            "d",
            "--bogus",
            "x"
        ]))
        .is_err());
        assert!(parse(&args(&["frobnicate", "x"])).is_err());
    }
}
