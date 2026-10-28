// File: tools/difftest/src/cli.rs
// Purpose: Parse command-line arguments into a typed Action
// Author: Akilesh
// Date: 2026-10-28

use std::path::PathBuf;

use crate::error::DifftestError;

const USAGE: &str = "usage:\n  difftest run --out <result.json> -- <command> [args...]\n  difftest compare <expected.json> <actual.json>";
const COMMAND_SEPARATOR: &str = "--";
const OUT_FLAG: &str = "--out";

#[derive(Debug, PartialEq, Eq)]
pub enum Action {
    Run { out: PathBuf, command: Vec<String> },
    Compare { expected: PathBuf, actual: PathBuf },
}

pub fn parse(args: &[String]) -> Result<Action, DifftestError> {
    match args.first().map(String::as_str) {
        Some("run") => parse_run(&args[1..]),
        Some("compare") => parse_compare(&args[1..]),
        _ => Err(usage_error()),
    }
}

fn parse_run(args: &[String]) -> Result<Action, DifftestError> {
    match args {
        [flag, out, separator, command @ ..]
            if flag == OUT_FLAG && separator == COMMAND_SEPARATOR && !command.is_empty() =>
        {
            Ok(Action::Run {
                out: PathBuf::from(out),
                command: command.to_vec(),
            })
        }
        _ => Err(usage_error()),
    }
}

fn parse_compare(args: &[String]) -> Result<Action, DifftestError> {
    match args {
        [expected, actual] => Ok(Action::Compare {
            expected: PathBuf::from(expected),
            actual: PathBuf::from(actual),
        }),
        _ => Err(usage_error()),
    }
}

fn usage_error() -> DifftestError {
    DifftestError::Usage(USAGE.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args(items: &[&str]) -> Vec<String> {
        items.iter().map(|item| item.to_string()).collect()
    }

    #[test]
    fn parses_run_with_command_arguments() {
        let action = parse(&args(&["run", "--out", "r.json", "--", "./hello", "-v"]));
        assert_eq!(
            action.expect("valid run"),
            Action::Run {
                out: PathBuf::from("r.json"),
                command: args(&["./hello", "-v"]),
            }
        );
    }

    #[test]
    fn parses_compare() {
        let action = parse(&args(&["compare", "a.json", "b.json"]));
        assert_eq!(
            action.expect("valid compare"),
            Action::Compare {
                expected: PathBuf::from("a.json"),
                actual: PathBuf::from("b.json"),
            }
        );
    }

    #[test]
    fn rejects_run_without_command() {
        assert!(parse(&args(&["run", "--out", "r.json", "--"])).is_err());
    }

    #[test]
    fn rejects_unknown_and_empty_input() {
        assert!(parse(&args(&["frobnicate"])).is_err());
        assert!(parse(&[]).is_err());
        assert!(parse(&args(&["compare", "only-one.json"])).is_err());
    }
}
