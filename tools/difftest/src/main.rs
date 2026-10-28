// File: tools/difftest/src/main.rs
// Purpose: difftest entry point; exit code 0 = match, 1 = mismatch, 2 = error
// Author: Akilesh
// Date: 2026-10-28

use std::path::Path;
use std::process::ExitCode;

use difftest::cli::{self, Action};
use difftest::error::DifftestError;
use difftest::{compare, result, runner};

const EXIT_MISMATCH: u8 = 1;
const EXIT_ERROR: u8 = 2;

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match cli::parse(&args).and_then(execute) {
        Ok(is_match) if is_match => ExitCode::SUCCESS,
        Ok(_) => ExitCode::from(EXIT_MISMATCH),
        Err(error) => {
            eprintln!("difftest: {error}");
            ExitCode::from(EXIT_ERROR)
        }
    }
}

/// Returns `Ok(true)` when the action succeeded and, for compare, the results match.
fn execute(action: Action) -> Result<bool, DifftestError> {
    match action {
        Action::Run { out, spec } => {
            let outcome = runner::run(&spec)?;
            result::save(&outcome, &out)?;
            Ok(true)
        }
        Action::Compare { expected, actual } => compare_files(&expected, &actual),
    }
}

fn compare_files(expected: &Path, actual: &Path) -> Result<bool, DifftestError> {
    let mismatches = compare::compare(&result::load(expected)?, &result::load(actual)?);
    for mismatch in &mismatches {
        eprintln!("{mismatch}");
    }
    Ok(mismatches.is_empty())
}
