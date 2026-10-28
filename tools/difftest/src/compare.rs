// File: tools/difftest/src/compare.rs
// Purpose: Compare an expected and an actual RunResult and report every mismatch
// Author: Akilesh
// Date: 2026-10-28

use std::fmt;

use crate::result::RunResult;

#[derive(Debug, PartialEq, Eq)]
pub struct Mismatch {
    pub field: &'static str,
    pub expected: String,
    pub actual: String,
}

impl fmt::Display for Mismatch {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} differs\n  expected: {:?}\n  actual:   {:?}",
            self.field, self.expected, self.actual
        )
    }
}

/// Strict byte comparison. Line endings are never normalized: a program whose
/// output differs by `\r\n` versus `\n` is a real translation bug.
pub fn compare(expected: &RunResult, actual: &RunResult) -> Vec<Mismatch> {
    let mut mismatches = Vec::new();
    if expected.exit_code != actual.exit_code {
        mismatches.push(Mismatch {
            field: "exit_code",
            expected: format!("{:?}", expected.exit_code),
            actual: format!("{:?}", actual.exit_code),
        });
    }
    push_if_different(&mut mismatches, "stdout", &expected.stdout, &actual.stdout);
    push_if_different(&mut mismatches, "stderr", &expected.stderr, &actual.stderr);
    mismatches
}

fn push_if_different(
    mismatches: &mut Vec<Mismatch>,
    field: &'static str,
    expected: &str,
    actual: &str,
) {
    if expected != actual {
        mismatches.push(Mismatch {
            field,
            expected: expected.to_string(),
            actual: actual.to_string(),
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> RunResult {
        RunResult {
            command: vec!["linux/hello".to_string()],
            exit_code: Some(3),
            stdout: "hello\n".to_string(),
            stderr: "err\n".to_string(),
        }
    }

    #[test]
    fn identical_results_have_no_mismatches() {
        let mut other = sample();
        other.command = vec!["windows/hello.exe".to_string()];
        assert!(compare(&sample(), &other).is_empty());
    }

    #[test]
    fn exit_code_mismatch_is_reported() {
        let mut other = sample();
        other.exit_code = Some(0);
        let mismatches = compare(&sample(), &other);
        assert_eq!(mismatches.len(), 1);
        assert_eq!(mismatches[0].field, "exit_code");
    }

    #[test]
    fn signal_versus_exit_code_is_a_mismatch() {
        let mut other = sample();
        other.exit_code = None;
        assert_eq!(compare(&sample(), &other)[0].field, "exit_code");
    }

    #[test]
    fn stdout_mismatch_is_reported() {
        let mut other = sample();
        other.stdout = "hello\r\n".to_string();
        let mismatches = compare(&sample(), &other);
        assert_eq!(mismatches.len(), 1);
        assert_eq!(mismatches[0].field, "stdout");
    }

    #[test]
    fn every_differing_field_is_reported() {
        let mut other = sample();
        other.exit_code = Some(1);
        other.stdout.clear();
        other.stderr.clear();
        assert_eq!(compare(&sample(), &other).len(), 3);
    }
}
