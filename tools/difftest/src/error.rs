// File: tools/difftest/src/error.rs
// Purpose: Typed error for the difftest runner; library code never panics or exits
// Author: Akilesh
// Date: 2026-10-28

use std::fmt;

#[derive(Debug)]
pub enum DifftestError {
    Usage(String),
    Io {
        context: String,
        source: std::io::Error,
    },
    Json {
        context: String,
        source: serde_json::Error,
    },
    /// A safety limit (output size, file count) was exceeded.
    Limit(String),
}

impl fmt::Display for DifftestError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Usage(message) | Self::Limit(message) => write!(f, "{message}"),
            Self::Io { context, source } => write!(f, "{context}: {source}"),
            Self::Json { context, source } => write!(f, "{context}: {source}"),
        }
    }
}

impl std::error::Error for DifftestError {}
