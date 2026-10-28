// File: tools/difftest/src/result.rs
// Purpose: Recorded outcome of one command run, plus JSON load and save
// Author: Akilesh
// Date: 2026-10-28

use std::collections::BTreeMap;
use std::fs;
use std::path::Path;

use serde::{Deserialize, Serialize};

use crate::error::DifftestError;

/// Size and SHA-256 of one file a program produced.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FileRecord {
    pub size: u64,
    pub sha256: String,
}

/// Outcome of one run. `command` is informational only: it differs per
/// platform and is never compared. Output is stored as lossy UTF-8.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RunResult {
    pub command: Vec<String>,
    /// `None` when the process was terminated by a signal or killed on timeout.
    pub exit_code: Option<i32>,
    pub stdout: String,
    pub stderr: String,
    /// Files found under the capture directory after the run, by relative path with `/` separators.
    #[serde(default)]
    pub files: BTreeMap<String, FileRecord>,
    /// The run exceeded its time limit and was killed.
    #[serde(default)]
    pub timed_out: bool,
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

    fn sample() -> RunResult {
        RunResult {
            command: vec!["hello".to_string()],
            exit_code: Some(3),
            stdout: "out\n".to_string(),
            stderr: "err\n".to_string(),
            files: BTreeMap::from([(
                "a/b.txt".to_string(),
                FileRecord {
                    size: 3,
                    sha256: "ab".repeat(32),
                },
            )]),
            timed_out: false,
        }
    }

    #[test]
    fn save_then_load_round_trips() {
        let dir = std::env::temp_dir().join("difftest-roundtrip");
        fs::create_dir_all(&dir).expect("temp dir is creatable");
        let path = dir.join("result.json");
        save(&sample(), &path).expect("save succeeds");
        assert_eq!(load(&path).expect("load succeeds"), sample());
    }

    #[test]
    fn results_recorded_before_files_existed_still_load() {
        let dir = std::env::temp_dir().join("difftest-legacy");
        fs::create_dir_all(&dir).expect("temp dir is creatable");
        let path = dir.join("legacy.json");
        fs::write(
            &path,
            r#"{"command":["x"],"exit_code":0,"stdout":"","stderr":""}"#,
        )
        .expect("writable");
        let loaded = load(&path).expect("legacy format loads");
        assert!(loaded.files.is_empty() && !loaded.timed_out);
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
