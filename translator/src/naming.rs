// File: translator/src/naming.rs
// Purpose: Validated library file names; the only way untrusted DT_NEEDED strings reach the file system
// Author: Akilesh
// Date: 2026-10-28

use crate::error::TranslateError;

const MAX_NAME_LEN: usize = 200;
const DLL_SUFFIX: &str = ".dll";

/// A plain file name such as `libfoo.so.1`. Construction rejects anything that
/// could escape a directory or confuse Windows (separators, `..`, drive
/// letters, control characters), so a hostile `DT_NEEDED` cannot read or write
/// outside the chosen directories.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct ModuleName(String);

fn is_allowed(character: char) -> bool {
    character.is_ascii_alphanumeric() || matches!(character, '.' | '_' | '+' | '-')
}

impl ModuleName {
    pub fn parse(raw: &str) -> Result<Self, TranslateError> {
        let is_valid = !raw.is_empty()
            && raw.len() <= MAX_NAME_LEN
            && raw.chars().all(is_allowed)
            && !raw.starts_with('.')
            && !raw.contains("..");
        if is_valid {
            Ok(Self(raw.to_string()))
        } else {
            Err(TranslateError::Unsupported(format!(
                "library name {raw:?} is not a plain file name"
            )))
        }
    }

    /// For the executable's own name, which comes from a local path rather than
    /// the binary: replace disallowed characters instead of failing.
    pub fn sanitized(raw: &str) -> Result<Self, TranslateError> {
        let cleaned: String = raw
            .chars()
            .take(MAX_NAME_LEN)
            .map(|c| if is_allowed(c) { c } else { '_' })
            .collect();
        Self::parse(cleaned.trim_start_matches('.'))
    }

    pub fn as_str(&self) -> &str {
        &self.0
    }

    /// The DLL file name this module is written under.
    pub fn dll_file_name(&self) -> String {
        format!("{}{DLL_SUFFIX}", self.0)
    }

    /// Windows file names are case-insensitive; this is the collision key.
    pub fn case_folded(&self) -> String {
        self.0.to_ascii_lowercase()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_ordinary_library_names() {
        for name in ["libc.so.6", "libfoo-1.2+x.so", "libm_ath.so.1"] {
            assert!(ModuleName::parse(name).is_ok(), "{name}");
        }
    }

    #[test]
    fn rejects_paths_and_oddities() {
        for name in [
            "",
            "../libx.so",
            "a/b.so",
            "a\\b.so",
            "C:evil.dll",
            ".hidden",
            "lib..so",
            "a b.so",
            "lib\0.so",
            "lib\n.so",
            "libé.so",
        ] {
            assert!(ModuleName::parse(name).is_err(), "{name:?}");
        }
        assert!(ModuleName::parse(&"a".repeat(MAX_NAME_LEN + 1)).is_err());
    }

    #[test]
    fn dll_name_appends_suffix_and_fold_ignores_case() {
        let name = ModuleName::parse("LibFoo.so.1").expect("valid");
        assert_eq!(name.dll_file_name(), "LibFoo.so.1.dll");
        assert_eq!(name.case_folded(), "libfoo.so.1");
    }

    #[test]
    fn sanitized_replaces_instead_of_failing() {
        assert_eq!(
            ModuleName::sanitized("my app!").expect("ok").as_str(),
            "my_app_"
        );
        assert!(ModuleName::sanitized("...").is_err());
    }
}
