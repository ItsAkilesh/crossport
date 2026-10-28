// File: translator/src/error.rs
// Purpose: Typed error for the translator; library code never panics or exits
// Author: Akilesh
// Date: 2026-10-28

use std::fmt;

#[derive(Debug)]
pub enum TranslateError {
    Usage(String),
    Io { context: String, source: std::io::Error },
    /// The input is not a well-formed ELF file.
    Elf(String),
    /// The input is valid but uses a feature Crossport does not translate yet.
    Unsupported(String),
}

impl fmt::Display for TranslateError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Usage(message) => write!(f, "{message}"),
            Self::Io { context, source } => write!(f, "{context}: {source}"),
            Self::Elf(message) => write!(f, "malformed ELF: {message}"),
            Self::Unsupported(message) => write!(f, "unsupported: {message}"),
        }
    }
}

impl std::error::Error for TranslateError {}
