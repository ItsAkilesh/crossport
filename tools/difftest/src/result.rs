// File: tools/difftest/src/result.rs
// Purpose: Recorded outcome of one command run, plus JSON load and save
// Author: Akilesh
// Date: 2026-10-28

use std::fs;
use std::path::Path;

use serde::{Deserialize, Serialize};

use crate::error::DifftestError;

/// Outcome of one run. `command` is informational only: it differs per
/// platform and is never compared. Output is stored as lossy UTF-8.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RunResult {
    pub command: Vec<String>,
    /// `None` when the process was terminated by a signal.
    pub exit_code: Option<i32>,
    pub stdout: String,
    pub stderr: String,
}

pub fn save(result: &RunResult, path: &Path) -> Result<(), DifftestError> {
    let json = serde_json::to_string_pretty(result).map_err(|source| DifftestError::Json {
        context: "serializing result".to_string(),
        source,
    })?;
    fs::write(path, json).map_err(|source| DifftestError::Io {
        context: format!("writing {}", path.display()),
        source,
    })
}

pub fn load(path: &Path) -> Result<RunResult, DifftestError> {
    let text = fs::read_to_string(path).map_err(|source| DifftestError::Io {
        context: format!("reading {}", path.display()),
        source,
    })?;
    serde_json::from_str(&text).map_err(|source| DifftestError::Json {
        context: format!("parsing {}", path.display()),
        source,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn save_then_load_round_trips() {
        let dir = std::env::temp_dir().join("difftest-roundtrip");
        fs::create_dir_all(&dir).expect("temp dir is creatable");
        let path = dir.join("result.json");
        let original = RunResult {
            command: vec!["hello".to_string()],
            exit_code: Some(3),
            stdout: "out\n".to_string(),
            stderr: "err\n".to_string(),
        };
        save(&original, &path).expect("save succeeds");
        assert_eq!(load(&path).expect("load succeeds"), original);
    }

    #[test]
    fn load_missing_file_is_io_error() {
        let path = std::env::temp_dir().join("difftest-does-not-exist.json");
        assert!(matches!(load(&path), Err(DifftestError::Io { .. })));
    }

    #[test]
    fn load_invalid_json_is_json_error() {
        let path = std::env::temp_dir().join("difftest-invalid.json");
        fs::write(&path, "not json").expect("temp file is writable");
        assert!(matches!(load(&path), Err(DifftestError::Json { .. })));
    }
}
