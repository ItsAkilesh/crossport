// File: tools/difftest/src/runner.rs
// Purpose: Run a command to completion and capture its exit code, stdout and stderr
// Author: Akilesh
// Date: 2026-10-28

use std::process::Command;

use crate::error::DifftestError;
use crate::result::RunResult;

/// Runs `command[0]` with the remaining items as arguments. Hung processes are
/// bounded by the CI job timeout rather than a runner-level timeout.
pub fn run_command(command: &[String]) -> Result<RunResult, DifftestError> {
    let (program, args) = command
        .split_first()
        .ok_or_else(|| DifftestError::Usage("no command given to run".to_string()))?;
    let output = Command::new(program)
        .args(args)
        .output()
        .map_err(|source| DifftestError::Io {
            context: format!("running {program}"),
            source,
        })?;
    Ok(RunResult {
        command: command.to_vec(),
        exit_code: output.status.code(),
        stdout: String::from_utf8_lossy(&output.stdout).into_owned(),
        stderr: String::from_utf8_lossy(&output.stderr).into_owned(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn captures_output_and_exit_code() {
        let result = run_command(&["cargo".to_string(), "--version".to_string()])
            .expect("cargo is available in the test environment");
        assert_eq!(result.exit_code, Some(0));
        assert!(result.stdout.starts_with("cargo "));
    }

    #[test]
    fn nonexistent_program_is_io_error() {
        let result = run_command(&["crossport-no-such-program".to_string()]);
        assert!(matches!(result, Err(DifftestError::Io { .. })));
    }

    #[test]
    fn empty_command_is_usage_error() {
        assert!(matches!(run_command(&[]), Err(DifftestError::Usage(_))));
    }
}
