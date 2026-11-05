// File: installer/src/names.rs
// Purpose: Turn AppImage file names and user input into safe Windows folder and display names
// Author: Akilesh
// Date: 2026-11-05

const APPIMAGE_EXTENSION: &str = ".appimage";
const MAX_NAME_CHARS: usize = 64;
const INVALID_CHARS: &str = "<>:\"/\\|?*";
const SIMPLE_DEVICES: [&str; 4] = ["CON", "PRN", "AUX", "NUL"];
const ARCH_WORDS: [&str; 5] = ["x86", "amd64", "x64", "linux", "appimage"];

/// Why a name could not be turned into a folder name.
#[derive(Debug, PartialEq, Eq)]
pub enum NameError {
    Empty,
}

/// A readable app name guessed from an AppImage file name: `KeePassXC-2.8.0-x86_64.AppImage` gives
/// `KeePassXC`. Parts after the first one that is a version or an architecture are dropped.
pub fn guess_app_name(file_name: &str) -> String {
    let stem = strip_extension(file_name);
    let kept: Vec<&str> = stem
        .split(['-', '_'])
        .take_while(|part| !is_noise(part))
        .collect();
    let guess = kept.join(" ");
    if guess.trim().is_empty() {
        stem.to_string()
    } else {
        guess
    }
}

fn strip_extension(file_name: &str) -> &str {
    let lower = file_name.to_ascii_lowercase();
    match lower.strip_suffix(APPIMAGE_EXTENSION) {
        Some(_) => &file_name[..file_name.len() - APPIMAGE_EXTENSION.len()],
        None => file_name,
    }
}

fn is_noise(part: &str) -> bool {
    let lower = part.to_ascii_lowercase();
    let starts_numeric = lower.chars().next().is_some_and(|c| c.is_ascii_digit());
    let is_version = lower
        .strip_prefix('v')
        .is_some_and(|rest| rest.chars().next().is_some_and(|c| c.is_ascii_digit()));
    starts_numeric || is_version || ARCH_WORDS.contains(&lower.as_str())
}

/// A name that is safe as one folder name under any install root.
pub fn folder_name(raw: &str) -> Result<String, NameError> {
    let cleaned: String = raw
        .chars()
        .map(|c| {
            if c.is_control() || INVALID_CHARS.contains(c) {
                '_'
            } else {
                c
            }
        })
        .take(MAX_NAME_CHARS)
        .collect();
    let trimmed = cleaned.trim_matches(|c| c == '.' || c == ' ');
    if trimmed.is_empty() {
        return Err(NameError::Empty);
    }
    Ok(avoid_device_name(trimmed))
}

fn avoid_device_name(name: &str) -> String {
    let base = name.split('.').next().unwrap_or(name).to_ascii_uppercase();
    if is_device(&base) {
        format!("_{name}")
    } else {
        name.to_string()
    }
}

fn is_device(base: &str) -> bool {
    if SIMPLE_DEVICES.contains(&base) {
        return true;
    }
    let numbered = base.strip_prefix("COM").or_else(|| base.strip_prefix("LPT"));
    matches!(numbered, Some(n) if n.len() == 1 && n != "0" && n.chars().all(|c| c.is_ascii_digit()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn guesses_from_versioned_names() {
        assert_eq!(guess_app_name("KeePassXC-2.8.0-x86_64.AppImage"), "KeePassXC");
        assert_eq!(guess_app_name("Cool_App-v1.2-x86_64.appimage"), "Cool App");
        assert_eq!(guess_app_name("lua.AppImage"), "lua");
        assert_eq!(guess_app_name("my-editor.AppImage"), "my editor");
    }

    #[test]
    fn falls_back_to_the_stem_when_everything_is_noise() {
        assert_eq!(guess_app_name("2048.AppImage"), "2048");
        assert_eq!(guess_app_name("x86_64.AppImage"), "x86_64");
    }

    #[test]
    fn folder_names_drop_invalid_characters() {
        assert_eq!(folder_name("a<b>c:d").as_deref(), Ok("a_b_c_d"));
    }

    #[test]
    fn traversal_cannot_survive() {
        assert_eq!(folder_name("../.."), Ok("_".to_string()));
        let name = folder_name(r"..\..\x").unwrap();
        assert!(!name.contains(['/', '\\']));
    }

    #[test]
    fn rejects_empty_and_dots() {
        assert_eq!(folder_name("   "), Err(NameError::Empty));
        assert_eq!(folder_name("..."), Err(NameError::Empty));
    }

    #[test]
    fn avoids_device_names() {
        assert_eq!(folder_name("con").as_deref(), Ok("_con"));
        assert_eq!(folder_name("COM1").as_deref(), Ok("_COM1"));
        assert_eq!(folder_name("nul.txt").as_deref(), Ok("_nul.txt"));
        assert_eq!(folder_name("COM0").as_deref(), Ok("COM0"));
        assert_eq!(folder_name("console").as_deref(), Ok("console"));
    }

    #[test]
    fn long_names_are_cut() {
        assert_eq!(folder_name(&"a".repeat(200)).unwrap().len(), MAX_NAME_CHARS);
    }
}
