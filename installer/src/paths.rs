// File: installer/src/paths.rs
// Purpose: Install locations, install scope and the rule for when elevation is needed
// Author: Akilesh
// Date: 2026-11-05

use std::path::{Path, PathBuf};

use crate::names::{folder_name, NameError};

/// Folder under each install root that holds Crossport and, beside it, the apps it installs.
pub const PRODUCT_DIR: &str = "Crossport";
const USER_PROGRAMS_DIR: &str = "Programs";

/// Who an installation belongs to, which decides the registry hive and the shortcut folders.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Scope {
    /// The current user only. Needs no elevation.
    User,
    /// Every user of the machine. Needs administrator rights to write.
    Machine,
}

/// The folders installs go under, read from the environment by the caller.
#[derive(Debug, Clone)]
pub struct Roots {
    pub program_files: PathBuf,
    pub local_app_data: PathBuf,
}

/// Where an install will go and what it takes to get it there.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Placement {
    pub scope: Scope,
    pub needs_elevation: bool,
}

impl Roots {
    /// The default folder for Crossport itself in a scope.
    pub fn product_dir(&self, scope: Scope) -> PathBuf {
        match scope {
            Scope::Machine => self.program_files.join(PRODUCT_DIR),
            Scope::User => self
                .local_app_data
                .join(USER_PROGRAMS_DIR)
                .join(PRODUCT_DIR),
        }
    }

    /// The default folder for one app in a scope: `<product dir>\<app name>`.
    pub fn app_dir(&self, scope: Scope, app_name: &str) -> Result<PathBuf, NameError> {
        Ok(self.product_dir(scope).join(folder_name(app_name)?))
    }

    /// Decide the scope and whether elevation is needed for an install folder. `is_writable` says whether the
    /// current user can create files at that location (probed by the caller), `is_elevated` whether this
    /// process already has administrator rights.
    pub fn place(&self, target: &Path, is_writable: bool, is_elevated: bool) -> Placement {
        let in_program_files = is_inside(target, &self.program_files);
        let is_machine = in_program_files || !is_writable;
        Placement {
            scope: if is_machine { Scope::Machine } else { Scope::User },
            needs_elevation: is_machine && !is_elevated,
        }
    }
}

fn components(path: &Path) -> Vec<String> {
    path.to_string_lossy()
        .split(['/', '\\'])
        .filter(|part| !part.is_empty() && *part != ".")
        .map(str::to_lowercase)
        .collect()
}

/// True when `path` is `root` or lies below it. Comparison ignores case and the kind of slash, and a path with
/// `..` is never inside anything, so a typed `C:\Program Files\..\Temp` cannot pass as a Program Files path.
pub fn is_inside(path: &Path, root: &Path) -> bool {
    let target = components(path);
    let base = components(root);
    if target.iter().any(|part| part == "..") || base.is_empty() {
        return false;
    }
    target.starts_with(&base)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn roots() -> Roots {
        Roots {
            program_files: PathBuf::from(r"C:\Program Files"),
            local_app_data: PathBuf::from(r"C:\Users\Ann\AppData\Local"),
        }
    }

    #[test]
    fn default_folders_follow_the_scope() {
        let r = roots();
        assert_eq!(r.product_dir(Scope::Machine), PathBuf::from(r"C:\Program Files").join("Crossport"));
        assert_eq!(
            r.app_dir(Scope::Machine, "KeePassXC").unwrap(),
            PathBuf::from(r"C:\Program Files").join("Crossport").join("KeePassXC")
        );
        let user = r.app_dir(Scope::User, "Lua").unwrap();
        assert!(user.ends_with(PathBuf::from("Programs").join("Crossport").join("Lua")));
    }

    #[test]
    fn app_names_are_made_safe() {
        let r = roots();
        let dir = r.app_dir(Scope::Machine, r"..\x").unwrap();
        assert!(dir.ends_with("Crossport\\_x") || dir.ends_with("Crossport/_x"));
        assert_eq!(r.app_dir(Scope::Machine, " . "), Err(NameError::Empty));
    }

    #[test]
    fn program_files_needs_elevation_unless_elevated() {
        let r = roots();
        let target = Path::new(r"C:\Program Files\Crossport\App");
        let plain = r.place(target, false, false);
        assert_eq!(plain, Placement { scope: Scope::Machine, needs_elevation: true });
        let admin = r.place(target, true, true);
        assert_eq!(admin, Placement { scope: Scope::Machine, needs_elevation: false });
    }

    #[test]
    fn a_writable_folder_elsewhere_is_per_user() {
        let r = roots();
        let placed = r.place(Path::new(r"D:\Apps\Thing"), true, false);
        assert_eq!(placed, Placement { scope: Scope::User, needs_elevation: false });
    }

    #[test]
    fn a_protected_folder_elsewhere_needs_elevation() {
        let r = roots();
        let placed = r.place(Path::new(r"C:\Tools\Thing"), false, false);
        assert_eq!(placed, Placement { scope: Scope::Machine, needs_elevation: true });
    }

    #[test]
    fn inside_ignores_case_and_slashes_but_not_dotdot() {
        let root = Path::new(r"C:\Program Files");
        assert!(is_inside(Path::new("c:/program files/Crossport"), root));
        assert!(is_inside(root, root));
        assert!(!is_inside(Path::new(r"C:\Program Files (x86)\X"), root));
        assert!(!is_inside(Path::new(r"C:\Program Files\..\Temp"), root));
    }
}
