// File: translator/src/appimage/paths.rs
// Purpose: Decide which archive paths may be written to a Windows directory; the extractor's safety gate
// Author: Akilesh
// Date: 2026-10-28

use std::path::Path;

use crate::error::TranslateError;

const MAX_COMPONENT_BYTES: usize = 255;
const MAX_DEPTH: usize = 64;
/// Both separators act as one on every platform (see `raw_parts`).
const SEPARATORS: [char; 2] = ['/', '\\'];
/// Characters Windows file names cannot contain (plus NUL, rejected separately).
const FORBIDDEN: [char; 9] = ['<', '>', ':', '"', '|', '?', '*', '\\', '/'];
const RESERVED_STEMS: [&str; 22] = [
    "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8",
    "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
];

fn unsafe_entry(path: &Path, reason: &str) -> TranslateError {
    TranslateError::Unsupported(format!("archive entry {} {reason}", path.display()))
}

fn is_reserved(component: &str) -> bool {
    let stem = component
        .split('.')
        .next()
        .unwrap_or("")
        .to_ascii_lowercase();
    RESERVED_STEMS.contains(&stem.as_str())
}

fn check_component(path: &Path, component: &str) -> Result<(), TranslateError> {
    if component.is_empty() || component.len() > MAX_COMPONENT_BYTES {
        return Err(unsafe_entry(path, "has an empty or overlong name"));
    }
    if component
        .chars()
        .any(|c| FORBIDDEN.contains(&c) || c.is_control())
    {
        return Err(unsafe_entry(
            path,
            "has a name with characters Windows cannot store",
        ));
    }
    if component.ends_with('.') || component.ends_with(' ') {
        return Err(unsafe_entry(
            path,
            "has a name ending in a dot or space, which Windows strips",
        ));
    }
    if is_reserved(component) {
        return Err(unsafe_entry(path, "uses a reserved Windows device name"));
    }
    Ok(())
}

/// The archive library builds entry paths with the host's `PathBuf`, so a Windows host sees `\`
/// where Linux sees `/`. Splitting on both on every platform makes the result identical everywhere
/// and means a backslash can only ever act as a separator, never smuggle a component past the checks.
fn raw_parts(path: &Path) -> Result<Vec<&str>, TranslateError> {
    let text = path
        .to_str()
        .ok_or_else(|| unsafe_entry(path, "is not valid UTF-8"))?;
    Ok(text.split(SEPARATORS).collect())
}

/// Splits an in-archive path into safe components. Absolute paths are taken relative to the
/// archive root (that is what they mean inside an AppDir), but `..` and every name Windows would
/// misinterpret (drive letters, reserved devices, control characters) are errors, never silently rewritten.
pub fn safe_components(path: &Path) -> Result<Vec<String>, TranslateError> {
    let mut components = Vec::new();
    for part in raw_parts(path)? {
        match part {
            "" | "." => {}
            ".." => return Err(unsafe_entry(path, "climbs out of the archive with '..'")),
            name => {
                check_component(path, name)?;
                components.push(name.to_string());
            }
        }
    }
    if components.len() > MAX_DEPTH {
        return Err(unsafe_entry(path, "is nested too deeply"));
    }
    Ok(components)
}

/// Windows paths are case-insensitive, so `A/b` and `a/B` collide.
pub fn case_key(components: &[String]) -> String {
    components.join("/").to_ascii_lowercase()
}

/// Resolves `target` (a symlink value) against the directory holding the link. Returns `None`
/// when it escapes the archive root, is absolute, or contains something unsafe.
pub fn resolve_link(link_components: &[String], target: &Path) -> Option<Vec<String>> {
    let text = target.to_str()?;
    if text.starts_with('/') {
        return None;
    }
    let mut resolved: Vec<String> =
        link_components[..link_components.len().saturating_sub(1)].to_vec();
    for part in text.split(SEPARATORS) {
        match part {
            "" | "." => {}
            ".." => {
                resolved.pop()?;
            }
            name => {
                check_component(target, name).ok()?;
                resolved.push(name.to_string());
            }
        }
    }
    Some(resolved)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn components(path: &str) -> Result<Vec<String>, TranslateError> {
        safe_components(Path::new(path))
    }

    #[test]
    fn ordinary_and_absolute_in_archive_paths_become_relative_components() {
        assert_eq!(
            components("usr/bin/tool").expect("ok"),
            ["usr", "bin", "tool"]
        );
        assert_eq!(
            components("/usr/lib/libx.so.1").expect("ok"),
            ["usr", "lib", "libx.so.1"]
        );
        assert_eq!(components("./a/./b").expect("ok"), ["a", "b"]);
        assert!(components("/").expect("root").is_empty());
    }

    #[test]
    fn a_backslash_is_only_ever_a_separator() {
        assert_eq!(components("a\\b\\c").expect("ok"), ["a", "b", "c"]);
        assert_eq!(
            components("\\usr\\bin\\tool").expect("ok"),
            ["usr", "bin", "tool"]
        );
    }

    #[test]
    fn every_way_out_of_the_destination_is_refused() {
        for bad in [
            "../evil",
            "a/../../evil",
            "usr/../../x",
            "a/..",
            "..\\evil",
            "a\\..\\..\\evil",
        ] {
            assert!(components(bad).is_err(), "{bad}");
        }
    }

    #[test]
    fn names_windows_would_misread_are_refused() {
        for bad in [
            "con",
            "CON.txt",
            "usr/aux",
            "NUL",
            "com1.log",
            "LPT9",
            "trailingdot.",
            "trailingspace ",
            "bad:name",
            "what?",
            "star*",
            "pipe|x",
            "quote\"x",
            "..\\..\\evil",
            "C:evil",
            "ctl\u{1}x",
        ] {
            assert!(components(bad).is_err(), "{bad:?}");
        }
        assert!(
            components("console").is_ok(),
            "only exact reserved stems are refused"
        );
        assert!(components("auxiliary.txt").is_ok());
    }

    #[test]
    fn overlong_and_too_deep_paths_are_refused() {
        assert!(components(&"a".repeat(MAX_COMPONENT_BYTES + 1)).is_err());
        assert!(components(&vec!["d"; MAX_DEPTH + 1].join("/")).is_err());
        assert!(components(&vec!["d"; MAX_DEPTH].join("/")).is_ok());
    }

    #[test]
    fn case_keys_detect_windows_collisions() {
        let upper = components("Usr/Lib/Foo.so").expect("ok");
        let lower = components("usr/lib/foo.so").expect("ok");
        assert_eq!(case_key(&upper), case_key(&lower));
    }

    fn link(path: &str) -> Vec<String> {
        components(path).expect("ok")
    }

    #[test]
    fn relative_links_resolve_inside_the_archive() {
        assert_eq!(
            resolve_link(&link("usr/lib/libx.so"), Path::new("libx.so.1.2")).expect("resolves"),
            ["usr", "lib", "libx.so.1.2"]
        );
        assert_eq!(
            resolve_link(&link("AppRun"), Path::new("usr/bin/tool")).expect("resolves"),
            ["usr", "bin", "tool"]
        );
        assert_eq!(
            resolve_link(&link("usr/bin/t"), Path::new("../lib/t")).expect("resolves"),
            ["usr", "lib", "t"]
        );
    }

    #[test]
    fn links_that_escape_or_point_at_absolute_paths_do_not_resolve() {
        assert!(resolve_link(&link("a"), Path::new("../outside")).is_none());
        assert!(resolve_link(&link("usr/a"), Path::new("../../outside")).is_none());
        assert!(resolve_link(&link("a"), Path::new("/etc/passwd")).is_none());
        assert!(resolve_link(&link("a"), Path::new("b/../../c")).is_none());
        assert!(resolve_link(&link("a"), Path::new("con")).is_none());
    }
}
