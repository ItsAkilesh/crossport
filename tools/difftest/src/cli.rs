// File: tools/difftest/src/cli.rs
// Purpose: Parse command-line arguments into a typed Action
// Author: Akilesh
// Date: 2026-10-28

use std::path::PathBuf;
use std::time::Duration;

use crate::error::DifftestError;
use crate::runner::{RunSpec, DEFAULT_TIMEOUT_SECS};

const USAGE: &str = "usage:\n  difftest run --out <result.json> [--stdin <file>] [--cwd <dir>] [--env KEY=VALUE]...\n               [--timeout <seconds>] [--capture-dir <dir>] -- <command> [args...]\n  difftest compare <expected.json> <actual.json>";
const COMMAND_SEPARATOR: &str = "--";
const MAX_TIMEOUT_SECS: u64 = 3600;

#[derive(Debug)]
pub enum Action {
    Run { out: PathBuf, spec: RunSpec },
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
    let separator = args
        .iter()
        .position(|arg| arg == COMMAND_SEPARATOR)
        .ok_or_else(usage_error)?;
    let command = &args[separator + 1..];
    if command.is_empty() {
        return Err(usage_error());
    }
    let mut spec = RunSpec::new(command.to_vec());
    let mut out = None;
    let mut flags = args[..separator].iter();
    while let Some(flag) = flags.next() {
        let value = flags.next().ok_or_else(usage_error)?;
        match flag.as_str() {
            "--out" if out.is_none() => out = Some(PathBuf::from(value)),
            "--stdin" if spec.stdin.is_none() => spec.stdin = Some(PathBuf::from(value)),
            "--cwd" if spec.cwd.is_none() => spec.cwd = Some(PathBuf::from(value)),
            "--capture-dir" if spec.capture_dir.is_none() => {
                spec.capture_dir = Some(PathBuf::from(value));
            }
            "--env" => spec.env.push(parse_env(value)?),
            "--timeout" => spec.timeout = parse_timeout(value)?,
            _ => return Err(usage_error()),
        }
    }
    Ok(Action::Run {
        out: out.ok_or_else(usage_error)?,
        spec,
    })
}

fn parse_env(text: &str) -> Result<(String, String), DifftestError> {
    match text.split_once('=') {
        Some((key, value)) if !key.is_empty() => Ok((key.to_string(), value.to_string())),
        _ => Err(DifftestError::Usage(format!(
            "--env expects KEY=VALUE, got {text:?}"
        ))),
    }
}

fn parse_timeout(text: &str) -> Result<Duration, DifftestError> {
    match text.parse::<u64>() {
        Ok(seconds) if (1..=MAX_TIMEOUT_SECS).contains(&seconds) => Ok(Duration::from_secs(seconds)),
        _ => Err(DifftestError::Usage(format!(
            "--timeout expects 1 to {MAX_TIMEOUT_SECS} seconds (default {DEFAULT_TIMEOUT_SECS}), got {text:?}"
        ))),
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

    fn run_spec(items: &[&str]) -> (PathBuf, RunSpec) {
        match parse(&args(items)).expect("valid run") {
            Action::Run { out, spec } => (out, spec),
            other => panic!("expected a run, got {other:?}"),
        }
    }

    #[test]
    fn parses_the_minimal_run() {
        let (out, spec) = run_spec(&["run", "--out", "r.json", "--", "./hello", "-v"]);
        assert_eq!(out, PathBuf::from("r.json"));
        assert_eq!(spec.command, args(&["./hello", "-v"]));
        assert_eq!(spec.timeout, Duration::from_secs(DEFAULT_TIMEOUT_SECS));
    }

    #[test]
    fn parses_every_option_in_any_order() {
        let (_, spec) = run_spec(&[
            "run",
            "--timeout",
            "5",
            "--env",
            "A=1",
            "--out",
            "r.json",
            "--env",
            "B=x=y",
            "--capture-dir",
            "work",
            "--cwd",
            "dir",
            "--stdin",
            "in.txt",
            "--",
            "prog",
        ]);
        assert_eq!(spec.timeout, Duration::from_secs(5));
        assert_eq!(
            spec.env,
            [("A".into(), "1".into()), ("B".into(), "x=y".into())]
        );
        assert_eq!(spec.capture_dir, Some(PathBuf::from("work")));
        assert_eq!(spec.cwd, Some(PathBuf::from("dir")));
        assert_eq!(spec.stdin, Some(PathBuf::from("in.txt")));
    }

    #[test]
    fn options_after_the_separator_belong_to_the_program() {
        let (_, spec) = run_spec(&["run", "--out", "r.json", "--", "prog", "--timeout", "9"]);
        assert_eq!(spec.command, args(&["prog", "--timeout", "9"]));
    }

    #[test]
    fn parses_compare() {
        assert!(matches!(
            parse(&args(&["compare", "a.json", "b.json"])).expect("valid"),
            Action::Compare { .. }
        ));
    }

    #[test]
    fn rejects_bad_input() {
        for bad in [
            args(&[]),
            args(&["frobnicate"]),
            args(&["run", "--out", "r.json", "--"]),
            args(&["run", "--", "prog"]),
            args(&["run", "--out", "r.json", "--timeout", "0", "--", "p"]),
            args(&["run", "--out", "r.json", "--timeout", "99999", "--", "p"]),
            args(&["run", "--out", "r.json", "--timeout", "soon", "--", "p"]),
            args(&["run", "--out", "r.json", "--env", "NOEQUALS", "--", "p"]),
            args(&["run", "--out", "r.json", "--env", "=v", "--", "p"]),
            args(&["run", "--out", "a", "--out", "b", "--", "p"]),
            args(&["run", "--out", "r.json", "--bogus", "x", "--", "p"]),
            args(&["run", "--out"]),
            args(&["compare", "only-one.json"]),
        ] {
            assert!(parse(&bad).is_err(), "{bad:?}");
        }
    }
}
