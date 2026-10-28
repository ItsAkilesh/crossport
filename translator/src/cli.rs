// File: translator/src/cli.rs
// Purpose: Parse command-line arguments into a typed Action
// Author: Akilesh
// Date: 2026-10-28

use std::path::PathBuf;

use crate::error::TranslateError;

const USAGE: &str = "usage:\n  crossport analyze <elf>\n  crossport translate <elf> --shim <crossport_libc.dll> --out <dir>";

#[derive(Debug, PartialEq, Eq)]
pub enum Action {
    Analyze { elf: PathBuf },
    Translate { elf: PathBuf, shim: PathBuf, out: PathBuf },
}

pub fn parse(args: &[String]) -> Result<Action, TranslateError> {
    match args {
        [command, elf] if command == "analyze" => Ok(Action::Analyze { elf: PathBuf::from(elf) }),
        [command, elf, rest @ ..] if command == "translate" => parse_translate(elf, rest),
        _ => Err(usage()),
    }
}

fn parse_translate(elf: &str, rest: &[String]) -> Result<Action, TranslateError> {
    match rest {
        [shim_flag, shim, out_flag, out] if shim_flag == "--shim" && out_flag == "--out" => {
            Ok(Action::Translate {
                elf: PathBuf::from(elf),
                shim: PathBuf::from(shim),
                out: PathBuf::from(out),
            })
        }
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
            Action::Analyze { elf: PathBuf::from("a.out") }
        );
    }

    #[test]
    fn parses_translate() {
        let action = parse(&args(&["translate", "a.out", "--shim", "s.dll", "--out", "dist"]));
        assert_eq!(
            action.expect("valid"),
            Action::Translate {
                elf: PathBuf::from("a.out"),
                shim: PathBuf::from("s.dll"),
                out: PathBuf::from("dist"),
            }
        );
    }

    #[test]
    fn rejects_incomplete_or_unknown_commands() {
        assert!(parse(&[]).is_err());
        assert!(parse(&args(&["translate", "a.out"])).is_err());
        assert!(parse(&args(&["translate", "a.out", "--out", "d", "--shim", "s"])).is_err());
        assert!(parse(&args(&["frobnicate", "x"])).is_err());
    }
}
