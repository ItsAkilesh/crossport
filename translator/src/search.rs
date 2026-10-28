// File: translator/src/search.rs
// Purpose: Turn RPATH, RUNPATH and default entries into AppDir-confined search directories, as the dynamic loader would
// Author: Akilesh
// Date: 2026-10-28

use std::path::Path;

use crate::appimage::paths::safe_components;

/// Where the loader looks when nothing else matches, relative to the AppDir root.
pub const DEFAULT_DIRECTORIES: [&str; 6] = [
    "usr/lib",
    "usr/lib/x86_64-linux-gnu",
    "usr/lib64",
    "lib",
    "lib/x86_64-linux-gnu",
    "lib64",
];
const LIB_TOKEN_VALUE: &str = "lib";
const PLATFORM_TOKEN_VALUE: &str = "x86_64";

/// Replaces the dynamic-string tokens. `None` when the entry uses a token we do not know or
/// needs `$ORIGIN` and none is available.
fn expand_tokens(entry: &str, origin: Option<&str>) -> Option<String> {
    let mut text = entry.to_string();
    for (token, value) in [
        ("ORIGIN", origin),
        ("LIB", Some(LIB_TOKEN_VALUE)),
        ("PLATFORM", Some(PLATFORM_TOKEN_VALUE)),
    ] {
        let wanted = text.contains(&format!("${token}")) || text.contains(&format!("${{{token}}}"));
        if wanted {
            let value = value?;
            text = text
                .replace(&format!("${{{token}}}"), value)
                .replace(&format!("${token}"), value);
        }
    }
    (!text.contains('$')).then_some(text)
}

/// Resolves one search entry to AppDir-relative components, or `None` if it cannot be used.
///
/// `origin` is the directory of the requesting module, relative to the AppDir root. Absolute
/// entries are read as paths inside the AppDir (a chroot-like view), relative entries other than
/// those starting with `$ORIGIN` depend on the working directory and are ignored, and any entry
/// that climbs above the AppDir root is dropped rather than clamped.
pub fn resolve_entry(entry: &str, origin: Option<&[String]>) -> Option<Vec<String>> {
    let origin_text = origin.map(|parts| parts.join("/"));
    let expanded = expand_tokens(entry, origin_text.as_deref())?;
    let is_anchored = entry.contains("$ORIGIN") || entry.contains("${ORIGIN}");
    let rooted = if is_anchored {
        expanded
    } else if expanded.starts_with('/') {
        expanded.trim_start_matches('/').to_string()
    } else {
        return None;
    };
    let mut resolved: Vec<String> = Vec::new();
    for part in rooted.split('/') {
        match part {
            "" | "." => {}
            ".." => {
                resolved.pop()?;
            }
            name => resolved.push(name.to_string()),
        }
    }
    safe_components(Path::new(&resolved.join("/"))).ok()
}

/// The directories to search, in the order glibc uses: `RPATH` (only when there is no `RUNPATH`),
/// then the caller's extra directories, then `RUNPATH`, then the defaults. Duplicates are dropped.
pub fn search_order(
    rpath: &[String],
    runpath: &[String],
    extra: &[Vec<String>],
    origin: Option<&[String]>,
) -> Vec<Vec<String>> {
    let mut ordered: Vec<Vec<String>> = Vec::new();
    let from_rpath = if runpath.is_empty() { rpath } else { &[] };
    let resolved_entries = from_rpath
        .iter()
        .filter_map(|entry| resolve_entry(entry, origin));
    let extras = extra.iter().cloned();
    let resolved_runpath = runpath
        .iter()
        .filter_map(|entry| resolve_entry(entry, origin));
    let defaults = DEFAULT_DIRECTORIES
        .iter()
        .filter_map(|entry| resolve_entry(&format!("/{entry}"), None));
    for directory in resolved_entries
        .chain(extras)
        .chain(resolved_runpath)
        .chain(defaults)
    {
        if !ordered.contains(&directory) {
            ordered.push(directory);
        }
    }
    ordered
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parts(items: &[&str]) -> Vec<String> {
        items.iter().map(|s| s.to_string()).collect()
    }

    #[test]
    fn origin_entries_resolve_relative_to_the_requesting_module() {
        let origin = parts(&["usr", "bin"]);
        assert_eq!(
            resolve_entry("$ORIGIN/../lib", Some(&origin)),
            Some(parts(&["usr", "lib"]))
        );
        assert_eq!(
            resolve_entry("${ORIGIN}", Some(&origin)),
            Some(parts(&["usr", "bin"]))
        );
        assert_eq!(
            resolve_entry("$ORIGIN/lib/x86_64-linux-gnu", Some(&origin)),
            Some(parts(&["usr", "bin", "lib", "x86_64-linux-gnu"]))
        );
    }

    #[test]
    fn absolute_entries_are_read_as_paths_inside_the_appdir() {
        assert_eq!(
            resolve_entry("/usr/lib/app", None),
            Some(parts(&["usr", "lib", "app"]))
        );
        assert_eq!(
            resolve_entry("//usr//lib/./x", None),
            Some(parts(&["usr", "lib", "x"]))
        );
    }

    #[test]
    fn entries_that_climb_out_of_the_appdir_are_dropped_not_clamped() {
        assert_eq!(
            resolve_entry("$ORIGIN/../../..", Some(&parts(&["usr", "bin"]))),
            None
        );
        assert_eq!(resolve_entry("/../etc", None), None);
        assert_eq!(resolve_entry("$ORIGIN/..", Some(&[])), None);
    }

    #[test]
    fn unusable_entries_are_ignored() {
        assert_eq!(
            resolve_entry("lib/relative", None),
            None,
            "depends on the working directory"
        );
        assert_eq!(
            resolve_entry("$ORIGIN/lib", None),
            None,
            "no origin available"
        );
        assert_eq!(resolve_entry("$UNKNOWN/lib", Some(&parts(&["a"]))), None);
        assert_eq!(resolve_entry("/usr/lib/$FUTURE", None), None);
        assert_eq!(
            resolve_entry("/con", None),
            None,
            "Windows device names are refused"
        );
        assert_eq!(resolve_entry("/a:b", None), None);
    }

    #[test]
    fn the_lib_and_platform_tokens_have_fixed_values() {
        assert_eq!(
            resolve_entry("/usr/$LIB/x", None),
            Some(parts(&["usr", "lib", "x"]))
        );
        assert_eq!(
            resolve_entry("/opt/${PLATFORM}", None),
            Some(parts(&["opt", "x86_64"]))
        );
    }

    #[test]
    fn rpath_is_ignored_when_a_runpath_exists() {
        let origin = parts(&["usr", "bin"]);
        let rpath = parts(&["$ORIGIN/old"]);
        let runpath = parts(&["$ORIGIN/../lib"]);
        let order = search_order(&rpath, &runpath, &[], Some(&origin));
        assert_eq!(order[0], parts(&["usr", "lib"]));
        assert!(!order.contains(&parts(&["usr", "bin", "old"])));
        let only_rpath = search_order(&rpath, &[], &[], Some(&origin));
        assert_eq!(only_rpath[0], parts(&["usr", "bin", "old"]));
    }

    #[test]
    fn the_order_is_rpath_then_extras_then_runpath_then_defaults_without_duplicates() {
        let origin = parts(&["usr", "bin"]);
        let extra = vec![parts(&["custom"])];
        let order = search_order(&parts(&["$ORIGIN/r"]), &[], &extra, Some(&origin));
        assert_eq!(order[0], parts(&["usr", "bin", "r"]));
        assert_eq!(order[1], parts(&["custom"]));
        assert_eq!(
            order
                .iter()
                .filter(|d| **d == parts(&["usr", "lib"]))
                .count(),
            1
        );
        let again = search_order(&parts(&["/usr/lib"]), &[], &[], None);
        assert_eq!(
            again
                .iter()
                .filter(|d| **d == parts(&["usr", "lib"]))
                .count(),
            1
        );
    }
}
