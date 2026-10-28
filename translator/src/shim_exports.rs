// File: translator/src/shim_exports.rs
// Purpose: The set of symbols the shim DLL exports, read from the built DLL so missing symbols fail at translation time
// Author: Akilesh
// Date: 2026-10-28

use std::collections::HashSet;

use object::read::pe::PeFile64;
use object::{NameOrOrdinal, Object};

use crate::error::TranslateError;

pub struct ShimExports {
    names: HashSet<String>,
}

impl ShimExports {
    pub fn from_names<I: IntoIterator<Item = String>>(names: I) -> Self {
        Self {
            names: names.into_iter().collect(),
        }
    }

    /// Reads the export table of a PE32+ DLL.
    pub fn from_dll(bytes: &[u8]) -> Result<Self, TranslateError> {
        let file = PeFile64::parse(bytes).map_err(|e| {
            TranslateError::Unsupported(format!("the shim is not a valid PE32+ file: {e}"))
        })?;
        let exports = Object::exports(&file).map_err(|e| {
            TranslateError::Unsupported(format!("cannot read the shim export table: {e}"))
        })?;
        let names = exports
            .map(|export| {
                export
                    .map(|e| match e.name() {
                        NameOrOrdinal::Name(name) => {
                            Some(String::from_utf8_lossy(name).into_owned())
                        }
                        NameOrOrdinal::Ordinal(_) => None,
                    })
                    .map_err(|e| TranslateError::Unsupported(format!("bad shim export entry: {e}")))
            })
            .collect::<Result<Vec<Option<String>>, TranslateError>>()?;
        Ok(Self::from_names(names.into_iter().flatten()))
    }

    pub fn provides(&self, name: &str) -> bool {
        self.names.contains(name)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn membership_is_exact() {
        let exports = ShimExports::from_names(["puts".to_string(), "crossport_start".to_string()]);
        assert!(exports.provides("puts"));
        assert!(!exports.provides("Puts"));
        assert!(!exports.provides("printf"));
    }

    #[test]
    fn garbage_is_not_a_shim() {
        assert!(ShimExports::from_dll(b"MZ not really").is_err());
    }
}
