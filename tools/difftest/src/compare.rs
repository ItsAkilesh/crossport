// File: tools/difftest/src/compare.rs
// Purpose: Compare an expected and an actual RunResult and report every mismatch
// Author: Akilesh
// Date: 2026-10-28

use std::collections::BTreeSet;
use std::fmt;

use crate::result::RunResult;

#[derive(Debug, PartialEq, Eq)]
pub struct Mismatch {
    pub field: String,
    pub expected: String,
    pub actual: String,
}

impl fmt::Display for Mismatch {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} differs\n  expected: {}\n  actual:   {}",
            self.field, self.expected, self.actual
        )
    }
}

fn mismatch(field: &str, expected: impl fmt::Debug, actual: impl fmt::Debug) -> Mismatch {
    Mismatch {
        field: field.to_string(),
        expected: format!("{expected:?}"),
        actual: format!("{actual:?}"),
    }
}

/// Strict byte comparison. Line endings are never normalized: a program whose
/// output differs by `\r\n` versus `\n` is a real translation bug.
pub fn compare(expected: &RunResult, actual: &RunResult) -> Vec<Mismatch> {
    let mut mismatches = Vec::new();
    if expected.exit_code != actual.exit_code {
        mismatches.push(mismatch("exit_code", expected.exit_code, actual.exit_code));
    }
    if expected.timed_out != actual.timed_out {
        mismatches.push(mismatch("timed_out", expected.timed_out, actual.timed_out));
    }
    if expected.stdout != actual.stdout {
        mismatches.push(mismatch("stdout", &expected.stdout, &actual.stdout));
    }
    if expected.stderr != actual.stderr {
        mismatches.push(mismatch("stderr", &expected.stderr, &actual.stderr));
    }
    compare_files(expected, actual, &mut mismatches);
    mismatches
}

/// Files present on only one side, or whose contents differ, each get their own mismatch.
fn compare_files(expected: &RunResult, actual: &RunResult, mismatches: &mut Vec<Mismatch>) {
    let paths: BTreeSet<&String> = expected.files.keys().chain(actual.files.keys()).collect();
    for path in paths {
        let field = format!("file {path}");
        match (expected.files.get(path), actual.files.get(path)) {
            (Some(left), Some(right)) if left != right => {
                mismatches.push(mismatch(&field, left, right));
            }
            (Some(left), None) => mismatches.push(mismatch(&field, left, "missing")),
            (None, Some(right)) => mismatches.push(mismatch(&field, "missing", right)),
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::result::FileRecord;
    use std::collections::BTreeMap;

    fn sample() -> RunResult {
        RunResult {
            command: vec!["linux/hello".to_string()],
            exit_code: Some(3),
            stdout: "hello\n".to_string(),
            stderr: "err\n".to_string(),
            files: BTreeMap::new(),
            timed_out: false,
        }
    }

    fn file(sha: &str) -> FileRecord {
        FileRecord {
            size: 1,
            sha256: sha.to_string(),
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
    fn stdout_mismatch_is_reported_and_line_endings_are_not_normalised() {
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
        other.timed_out = true;
        assert_eq!(compare(&sample(), &other).len(), 4);
    }

    #[test]
    fn differing_missing_and_extra_files_are_each_reported() {
        let mut left = sample();
        left.files.insert("same".into(), file("a"));
        left.files.insert("changed".into(), file("b"));
        left.files.insert("only-left".into(), file("c"));
        let mut right = sample();
        right.files.insert("same".into(), file("a"));
        right.files.insert("changed".into(), file("z"));
        right.files.insert("only-right".into(), file("d"));
        let fields: Vec<String> = compare(&left, &right)
            .into_iter()
            .map(|m| m.field)
            .collect();
        assert_eq!(
            fields,
            ["file changed", "file only-left", "file only-right"]
        );
    }
}
