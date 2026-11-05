// File: installer/src/regplan.rs
// Purpose: The registry entries an install writes (uninstall record, file association) and PATH editing
// Author: Akilesh
// Date: 2026-11-05

use std::path::Path;

use crate::paths::Scope;

const UNINSTALL_ROOT: &str = r"Software\Microsoft\Windows\CurrentVersion\Uninstall";
const CLASSES_ROOT: &str = r"Software\Classes";
pub const PROG_ID: &str = "Crossport.AppImage";
pub const EXTENSION: &str = ".AppImage";
const PUBLISHER: &str = "Crossport";
const OPENER_EXE: &str = "crossport-open.exe";

/// A registry hive an install writes to.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Hive {
    CurrentUser,
    LocalMachine,
}

impl Hive {
    pub fn for_scope(scope: Scope) -> Self {
        match scope {
            Scope::User => Hive::CurrentUser,
            Scope::Machine => Hive::LocalMachine,
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            Hive::CurrentUser => "HKCU",
            Hive::LocalMachine => "HKLM",
        }
    }
}

/// A value to store.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RegValue {
    Text(String),
    Number(u32),
}

/// One value in one key. `name` is `None` for the key's default value.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RegEntry {
    pub hive: Hive,
    pub key: String,
    pub name: Option<String>,
    pub value: RegValue,
}

fn text(hive: Hive, key: &str, name: Option<&str>, value: &str) -> RegEntry {
    RegEntry {
        hive,
        key: key.to_string(),
        name: name.map(str::to_string),
        value: RegValue::Text(value.to_string()),
    }
}

/// Everything an installed thing shows in Apps and features.
#[derive(Debug, Clone)]
pub struct UninstallInfo<'a> {
    pub id: &'a str,
    pub display_name: &'a str,
    pub version: &'a str,
    pub install_dir: &'a Path,
    pub uninstall_command: &'a str,
    pub icon: Option<&'a Path>,
    pub size_kb: u32,
}

/// The Apps and features key for an id, relative to its hive.
pub fn uninstall_key(id: &str) -> String {
    format!(r"{UNINSTALL_ROOT}\{id}")
}

/// The values that make an entry in Apps and features with a working Uninstall button.
pub fn uninstall_entries(scope: Scope, info: &UninstallInfo) -> Vec<RegEntry> {
    let hive = Hive::for_scope(scope);
    let key = uninstall_key(info.id);
    let dir = info.install_dir.to_string_lossy();
    let mut entries = vec![
        text(hive, &key, Some("DisplayName"), info.display_name),
        text(hive, &key, Some("DisplayVersion"), info.version),
        text(hive, &key, Some("Publisher"), PUBLISHER),
        text(hive, &key, Some("InstallLocation"), &dir),
        text(hive, &key, Some("UninstallString"), info.uninstall_command),
        text(hive, &key, Some("QuietUninstallString"), &format!("{} --quiet", info.uninstall_command)),
    ];
    if let Some(icon) = info.icon {
        entries.push(text(hive, &key, Some("DisplayIcon"), &icon.to_string_lossy()));
    }
    for flag in ["NoModify", "NoRepair"] {
        entries.push(RegEntry { hive, key: key.clone(), name: Some(flag.into()), value: RegValue::Number(1) });
    }
    entries.push(RegEntry { hive, key, name: Some("EstimatedSize".into()), value: RegValue::Number(info.size_kb) });
    entries
}

fn classes_key(rest: &str) -> String {
    format!(r"{CLASSES_ROOT}\{rest}")
}

/// Registers `.AppImage` files to open with the launcher. Windows 10 and 11 keep the user's chosen default
/// in a protected `UserChoice` key that only the user can set, so these entries make Crossport appear in
/// Open with and become the default when no other program has been chosen.
pub fn association_entries(scope: Scope, install_dir: &Path) -> Vec<RegEntry> {
    let hive = Hive::for_scope(scope);
    let opener = install_dir.join(OPENER_EXE);
    let opener = opener.to_string_lossy();
    let command = format!("\"{opener}\" \"%1\"");
    let prog = classes_key(PROG_ID);
    let app = classes_key(&format!(r"Applications\{OPENER_EXE}"));
    vec![
        text(hive, &classes_key(EXTENSION), None, PROG_ID),
        text(hive, &format!(r"{}\OpenWithProgids", classes_key(EXTENSION)), Some(PROG_ID), ""),
        text(hive, &prog, None, "Linux AppImage"),
        text(hive, &format!(r"{prog}\DefaultIcon"), None, &format!("{opener},0")),
        text(hive, &format!(r"{prog}\shell\open"), None, "Open with Crossport"),
        text(hive, &format!(r"{prog}\shell\open\command"), None, &command),
        text(hive, &format!(r"{app}\shell\open\command"), None, &command),
        text(hive, &format!(r"{app}\SupportedTypes"), Some(EXTENSION), ""),
    ]
}

/// The keys `association_entries` writes, to delete whole on uninstall.
pub fn association_keys(scope: Scope) -> Vec<(Hive, String)> {
    let hive = Hive::for_scope(scope);
    vec![
        (hive, classes_key(PROG_ID)),
        (hive, classes_key(&format!(r"Applications\{OPENER_EXE}"))),
    ]
}

const PATH_SEPARATOR: char = ';';

fn same_folder(a: &str, b: &str) -> bool {
    let norm = |s: &str| s.trim().trim_end_matches(['\\', '/']).replace('/', "\\").to_lowercase();
    norm(a) == norm(b)
}

/// `path` with `dir` added at the end, unless it is already there.
pub fn add_to_path(path: &str, dir: &Path) -> String {
    let dir = dir.to_string_lossy();
    if path.split(PATH_SEPARATOR).any(|p| same_folder(p, &dir)) {
        return path.to_string();
    }
    if path.trim().is_empty() {
        return dir.into_owned();
    }
    format!("{}{PATH_SEPARATOR}{dir}", path.trim_end_matches(PATH_SEPARATOR))
}

/// `path` without any entry equal to `dir`.
pub fn remove_from_path(path: &str, dir: &Path) -> String {
    let dir = dir.to_string_lossy();
    let kept: Vec<&str> = path.split(PATH_SEPARATOR).filter(|p| !same_folder(p, &dir)).collect();
    kept.join(&PATH_SEPARATOR.to_string())
}

/// Where the PATH variable lives for a scope: `(hive, key)`.
pub fn path_location(scope: Scope) -> (Hive, &'static str) {
    match scope {
        Scope::User => (Hive::CurrentUser, "Environment"),
        Scope::Machine => (
            Hive::LocalMachine,
            r"SYSTEM\CurrentControlSet\Control\Session Manager\Environment",
        ),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    fn value<'a>(entries: &'a [RegEntry], name: &str) -> &'a RegValue {
        &entries.iter().find(|e| e.name.as_deref() == Some(name)).unwrap().value
    }

    #[test]
    fn uninstall_entry_has_the_values_apps_and_features_reads() {
        let dir = PathBuf::from(r"C:\Program Files\Crossport\Lua");
        let info = UninstallInfo {
            id: "Crossport.App.Lua",
            display_name: "Lua",
            version: "5.4",
            install_dir: &dir,
            uninstall_command: "\"C:\\Program Files\\Crossport\\crossport-open.exe\" --uninstall \"C:\\Program Files\\Crossport\\Lua\"",
            icon: None,
            size_kb: 1200,
        };
        let entries = uninstall_entries(Scope::Machine, &info);
        assert!(entries.iter().all(|e| e.hive == Hive::LocalMachine));
        assert!(entries.iter().all(|e| e.key == r"Software\Microsoft\Windows\CurrentVersion\Uninstall\Crossport.App.Lua"));
        assert_eq!(value(&entries, "DisplayName"), &RegValue::Text("Lua".into()));
        assert_eq!(value(&entries, "NoModify"), &RegValue::Number(1));
        assert_eq!(value(&entries, "EstimatedSize"), &RegValue::Number(1200));
        match value(&entries, "QuietUninstallString") {
            RegValue::Text(t) => assert!(t.ends_with("--quiet")),
            other => panic!("unexpected {other:?}"),
        }
        assert!(!entries.iter().any(|e| e.name.as_deref() == Some("DisplayIcon")));
    }

    #[test]
    fn user_scope_writes_to_the_user_hive() {
        let dir = PathBuf::from("C:\\x");
        let info = UninstallInfo { id: "a", display_name: "A", version: "1", install_dir: &dir, uninstall_command: "u", icon: Some(&dir), size_kb: 0 };
        let entries = uninstall_entries(Scope::User, &info);
        assert!(entries.iter().all(|e| e.hive == Hive::CurrentUser));
        assert!(entries.iter().any(|e| e.name.as_deref() == Some("DisplayIcon")));
    }

    #[test]
    fn association_points_the_shell_at_the_launcher() {
        let entries = association_entries(Scope::User, Path::new(r"C:\Program Files\Crossport"));
        let open = entries.iter().find(|e| e.key.ends_with(r"Crossport.AppImage\shell\open\command")).unwrap();
        assert_eq!(
            open.value,
            RegValue::Text("\"C:\\Program Files\\Crossport\\crossport-open.exe\" \"%1\"".into())
        );
        let ext = entries.iter().find(|e| e.key == r"Software\Classes\.AppImage" && e.name.is_none()).unwrap();
        assert_eq!(ext.value, RegValue::Text("Crossport.AppImage".into()));
        assert!(entries.iter().all(|e| e.hive == Hive::CurrentUser));
    }

    #[test]
    fn association_keys_cover_what_is_written() {
        let keys = association_keys(Scope::Machine);
        assert!(keys.iter().all(|(h, _)| *h == Hive::LocalMachine));
        let entries = association_entries(Scope::Machine, Path::new("C:\\p"));
        let owned = |key: &str| keys.iter().any(|(_, k)| key == k || key.starts_with(&format!("{k}\\")));
        assert!(entries.iter().filter(|e| !e.key.ends_with(".AppImage") && !e.key.contains(r".AppImage\")).all(|e| owned(&e.key)));
    }

    #[test]
    fn path_editing_is_idempotent_and_reversible() {
        let dir = Path::new(r"C:\Program Files\Crossport");
        let base = r"C:\Windows;C:\Tools";
        let added = add_to_path(base, dir);
        assert_eq!(added, r"C:\Windows;C:\Tools;C:\Program Files\Crossport");
        assert_eq!(add_to_path(&added, Path::new(r"c:\program files\crossport\")), added);
        assert_eq!(remove_from_path(&added, dir), base);
        assert_eq!(add_to_path("", dir), r"C:\Program Files\Crossport");
        assert_eq!(add_to_path("C:\\a;", dir), r"C:\a;C:\Program Files\Crossport");
    }

    #[test]
    fn path_locations_follow_the_scope() {
        assert_eq!(path_location(Scope::User).0, Hive::CurrentUser);
        assert_eq!(path_location(Scope::Machine).0, Hive::LocalMachine);
    }
}
