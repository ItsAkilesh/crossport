// File: translator/src/error.rs
// Purpose: Typed error for the translator; library code never panics or exits
// Author: Akilesh
// Date: 2026-10-28

use std::fmt;

#[derive(Debug)]
pub enum TranslateError {
    Usage(String),
    Io {
        context: String,
        source: std::io::Error,
    },
    /// The input is not a well-formed ELF file.
    Elf(String),
    /// The input is valid but uses a feature Crossport does not translate yet.
    Unsupported(String),
    /// A `DT_NEEDED` library was not found in any search directory.
    MissingLibrary {
        name: String,
    },
    /// A strongly referenced symbol is defined by no module and not provided by the shim.
    UnresolvedSymbol {
        symbol: String,
        module: String,
    },
    /// Strongly referenced symbols defined by no module and not provided by the shim, all at once.
    UnresolvedSymbols(Vec<String>),
    /// The library dependency graph contains a cycle.
    DependencyCycle(String),
    /// An input exceeds a safety limit.
    LimitExceeded(String),
}

impl fmt::Display for TranslateError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Usage(message) => write!(f, "{message}"),
            Self::Io { context, source } => write!(f, "{context}: {source}"),
            Self::Elf(message) => write!(f, "malformed ELF: {message}"),
            Self::Unsupported(message) => write!(f, "unsupported: {message}"),
            Self::MissingLibrary { name } => {
                write!(f, "library {name} was not found in any --lib-dir")
            }
            Self::UnresolvedSymbol { symbol, module } => write!(
                f,
                "symbol {symbol} (needed by {module}) is not defined by any bundled library or the shim"
            ),
            Self::UnresolvedSymbols(symbols) => write!(f, "{}", describe_missing(symbols)),
            Self::DependencyCycle(path) => write!(f, "library dependency cycle: {path}"),
            Self::LimitExceeded(message) => write!(f, "limit exceeded: {message}"),
        }
    }
}

const MAX_LISTED_SYMBOLS: usize = 40;

fn describe_missing(symbols: &[String]) -> String {
    let shown: Vec<&str> = symbols
        .iter()
        .take(MAX_LISTED_SYMBOLS)
        .map(String::as_str)
        .collect();
    let more = symbols.len().saturating_sub(MAX_LISTED_SYMBOLS);
    let suffix = if more > 0 {
        format!(" (and {more} more)")
    } else {
        String::new()
    };
    format!(
        "{} symbols are not defined by any bundled library or the shim: {}{suffix}",
        symbols.len(),
        shown.join(", ")
    )
}

impl std::error::Error for TranslateError {}
