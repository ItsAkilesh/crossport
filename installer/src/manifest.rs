// File: installer/src/manifest.rs
// Purpose: The uninstall manifest, a record of everything one install created, read back to remove it
// Author: Akilesh
// Date: 2026-11-05

use std::path::PathBuf;

use crate::paths::Scope;

/// File name of the manifest, written into the install folder.
pub const MANIFEST_FILE: &str = "crossport-uninstall.manifest";
const MAGIC: &str = "crossport-manifest 1";

/// What an install created outside its own folder, plus how to find that folder again.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Manifest {
    pub name: String,
    pub scope: Scope,
    pub dir: PathBuf,
    /// Registry keys to delete whole, as `(hive, subkey)`.
    pub keys: Vec<(String, String)>,
    /// Shortcut files to delete.
    pub shortcuts: Vec<PathBuf>,
    /// Folders added to the PATH variable, to take out again.
    pub path_entries: Vec<PathBuf>,
    /// Files to delete one by one (used when the folder is shared with files that are not ours).
    pub files: Vec<String>,
}

/// Why a manifest could not be read or written.
#[derive(Debug, PartialEq, Eq)]
pub enum ManifestError {
    BadHeader,
    BadLine(usize),
    MissingField(&'static str),
    UnsafeValue(String),
}

impl Manifest {
    pub fn new(name: &str, scope: Scope, dir: PathBuf) -> Self {
        Self {
            name: name.to_string(),
            scope,
            dir,
            keys: Vec::new(),
            shortcuts: Vec::new(),
            path_entries: Vec::new(),
            files: Vec::new(),
        }
    }

    /// Text form: one `kind<TAB>value` line per item.
    pub fn serialize(&self) -> Result<String, ManifestError> {
        let mut lines = vec![MAGIC.to_string()];
        let scope = if self.scope == Scope::Machine { "machine" } else { "user" };
        lines.push(line("name", &[&self.name])?);
        lines.push(line("scope", &[scope])?);
        lines.push(line("dir", &[&self.dir.to_string_lossy()])?);
        for (hive, key) in &self.keys {
            lines.push(line("key", &[hive, key])?);
        }
        for path in &self.shortcuts {
            lines.push(line("shortcut", &[&path.to_string_lossy()])?);
        }
        for path in &self.path_entries {
            lines.push(line("path", &[&path.to_string_lossy()])?);
        }
        for file in &self.files {
            lines.push(line("file", &[file])?);
        }
        Ok(lines.join("\n") + "\n")
    }

    pub fn parse(text: &str) -> Result<Self, ManifestError> {
        let mut lines = text.lines();
        if lines.next() != Some(MAGIC) {
            return Err(ManifestError::BadHeader);
        }
        let mut manifest = Manifest::new("", Scope::User, PathBuf::new());
        let (mut has_name, mut has_dir) = (false, false);
        for (index, raw) in lines.enumerate() {
            let parts: Vec<&str> = raw.split('\t').collect();
            match parts.as_slice() {
                ["name", v] => (manifest.name, has_name) = (v.to_string(), true),
                ["scope", "machine"] => manifest.scope = Scope::Machine,
                ["scope", "user"] => manifest.scope = Scope::User,
                ["dir", v] => (manifest.dir, has_dir) = (PathBuf::from(v), true),
                ["key", hive, key] => manifest.keys.push((hive.to_string(), key.to_string())),
                ["shortcut", v] => manifest.shortcuts.push(PathBuf::from(v)),
                ["path", v] => manifest.path_entries.push(PathBuf::from(v)),
                ["file", v] => manifest.files.push(v.to_string()),
                _ => return Err(ManifestError::BadLine(index + 2)),
            }
        }
        if !has_name {
            return Err(ManifestError::MissingField("name"));
        }
        if !has_dir {
            return Err(ManifestError::MissingField("dir"));
        }
        Ok(manifest)
    }
}

fn line(kind: &str, values: &[&str]) -> Result<String, ManifestError> {
    for value in values {
        if value.contains(['\t', '\n', '\r']) {
            return Err(ManifestError::UnsafeValue(value.to_string()));
        }
    }
    Ok(format!("{kind}\t{}", values.join("\t")))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> Manifest {
        let mut m = Manifest::new("KeePassXC", Scope::Machine, PathBuf::from(r"C:\Program Files\Crossport\KeePassXC"));
        m.keys.push(("HKLM".into(), r"Software\Microsoft\Windows\CurrentVersion\Uninstall\Crossport.App.KeePassXC".into()));
        m.shortcuts.push(PathBuf::from(r"C:\ProgramData\Microsoft\Windows\Start Menu\Programs\KeePassXC.lnk"));
        m.path_entries.push(PathBuf::from(r"C:\Program Files\Crossport"));
        m.files.push("crossport.exe".into());
        m
    }

    #[test]
    fn round_trips() {
        let m = sample();
        assert_eq!(Manifest::parse(&m.serialize().unwrap()), Ok(m));
    }

    #[test]
    fn rejects_foreign_text() {
        assert_eq!(Manifest::parse("hello\n"), Err(ManifestError::BadHeader));
        assert_eq!(Manifest::parse(""), Err(ManifestError::BadHeader));
    }

    #[test]
    fn reports_the_bad_line() {
        let text = format!("{MAGIC}\nname\tX\nbogus\tY\n");
        assert_eq!(Manifest::parse(&text), Err(ManifestError::BadLine(3)));
    }

    #[test]
    fn requires_name_and_dir() {
        assert_eq!(
            Manifest::parse(&format!("{MAGIC}\nscope\tuser\n")),
            Err(ManifestError::MissingField("name"))
        );
        assert_eq!(
            Manifest::parse(&format!("{MAGIC}\nname\tX\n")),
            Err(ManifestError::MissingField("dir"))
        );
    }

    #[test]
    fn refuses_values_that_would_break_lines() {
        let mut m = sample();
        m.shortcuts.push(PathBuf::from("a\nb"));
        assert!(matches!(m.serialize(), Err(ManifestError::UnsafeValue(_))));
    }
}
