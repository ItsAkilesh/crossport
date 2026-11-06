// File: installer/src/cli.rs
// Purpose: Command-line grammar of the launcher and the setup program, including the arguments used to relaunch elevated
// Author: Akilesh
// Date: 2026-11-05

use std::path::PathBuf;

/// Options for a launcher run with no wizard.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct OpenSilent {
    pub image: PathBuf,
    pub name: Option<String>,
    pub dir: Option<PathBuf>,
    pub per_user: bool,
    pub start_menu: bool,
    pub desktop: bool,
    pub stub_missing: bool,
    /// Where an elevated copy writes its report for the copy that started it.
    pub result_file: Option<PathBuf>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum OpenCommand {
    Wizard { image: Option<PathBuf> },
    Silent(OpenSilent),
    Uninstall { dir: PathBuf, quiet: bool },
    /// Ask GitHub whether a newer Crossport exists (only when this flag is given, never on its own).
    CheckUpdate { quiet: bool },
}

/// Options for a setup run with no wizard.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct SetupSilent {
    pub dir: Option<PathBuf>,
    pub per_user: bool,
    pub start_menu: bool,
    pub desktop: bool,
    pub add_to_path: bool,
    pub associate: bool,
    pub result_file: Option<PathBuf>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SetupCommand {
    Wizard,
    Silent(SetupSilent),
    Uninstall { dir: PathBuf, quiet: bool },
    /// `uninstaller` is the program to install as uninstall.exe (a signed copy); default: this program.
    MakeInstaller { out: PathBuf, from: PathBuf, uninstaller: Option<PathBuf> },
    CheckUpdate { quiet: bool },
}

fn value(args: &mut std::slice::Iter<String>, flag: &str) -> Result<String, String> {
    args.next().cloned().ok_or_else(|| format!("{flag} needs a value"))
}

pub fn parse_open(args: &[String]) -> Result<OpenCommand, String> {
    let mut silent = OpenSilent { start_menu: true, ..OpenSilent::default() };
    let (mut is_silent, mut image, mut uninstall, mut quiet, mut check_update) = (false, None, None, false, false);
    let mut iter = args.iter();
    while let Some(arg) = iter.next() {
        match arg.as_str() {
            "--silent" => is_silent = true,
            "--check-update" => check_update = true,
            "--quiet" => quiet = true,
            "--uninstall" => uninstall = Some(PathBuf::from(value(&mut iter, arg)?)),
            "--name" => silent.name = Some(value(&mut iter, arg)?),
            "--dir" => silent.dir = Some(PathBuf::from(value(&mut iter, arg)?)),
            "--result-file" => silent.result_file = Some(PathBuf::from(value(&mut iter, arg)?)),
            "--user" => silent.per_user = true,
            "--no-start-menu" => silent.start_menu = false,
            "--desktop" => silent.desktop = true,
            "--stub-missing" => silent.stub_missing = true,
            other if other.starts_with("--") => return Err(format!("unknown option {other}")),
            other if image.is_none() => image = Some(PathBuf::from(other)),
            other => return Err(format!("unexpected argument {other}")),
        }
    }
    if let Some(dir) = uninstall {
        return Ok(OpenCommand::Uninstall { dir, quiet });
    }
    if check_update {
        return Ok(OpenCommand::CheckUpdate { quiet });
    }
    match (is_silent, image) {
        (true, Some(image)) => Ok(OpenCommand::Silent(OpenSilent { image, ..silent })),
        (true, None) => Err("--silent needs the AppImage to install".into()),
        (false, image) => Ok(OpenCommand::Wizard { image }),
    }
}

impl OpenSilent {
    /// The arguments that make another copy of the launcher do the same install.
    pub fn to_args(&self) -> Vec<String> {
        let mut args = vec![self.image.to_string_lossy().into_owned(), "--silent".to_string()];
        let mut push = |flag: &str, v: Option<String>| {
            if let Some(v) = v {
                args.extend([flag.to_string(), v]);
            }
        };
        push("--name", self.name.clone());
        push("--dir", self.dir.as_ref().map(|d| d.to_string_lossy().into_owned()));
        push("--result-file", self.result_file.as_ref().map(|d| d.to_string_lossy().into_owned()));
        for (on, flag) in [(self.per_user, "--user"), (!self.start_menu, "--no-start-menu"), (self.desktop, "--desktop"), (self.stub_missing, "--stub-missing")] {
            if on {
                args.push(flag.to_string());
            }
        }
        args
    }
}

pub fn parse_setup(args: &[String]) -> Result<SetupCommand, String> {
    let mut silent = SetupSilent { start_menu: true, associate: true, ..SetupSilent::default() };
    let (mut is_silent, mut uninstall, mut quiet, mut out, mut from, mut check_update) = (false, None, false, None, None, false);
    let mut uninstaller = None;
    let mut iter = args.iter();
    while let Some(arg) = iter.next() {
        match arg.as_str() {
            "--silent" => is_silent = true,
            "--check-update" => check_update = true,
            "--quiet" => quiet = true,
            "--uninstall" => uninstall = Some(PathBuf::from(value(&mut iter, arg)?)),
            "--make-installer" => out = Some(PathBuf::from(value(&mut iter, arg)?)),
            "--from" => from = Some(PathBuf::from(value(&mut iter, arg)?)),
            "--uninstaller" => uninstaller = Some(PathBuf::from(value(&mut iter, arg)?)),
            "--dir" => silent.dir = Some(PathBuf::from(value(&mut iter, arg)?)),
            "--result-file" => silent.result_file = Some(PathBuf::from(value(&mut iter, arg)?)),
            "--user" => silent.per_user = true,
            "--no-start-menu" => silent.start_menu = false,
            "--desktop" => silent.desktop = true,
            "--path" => silent.add_to_path = true,
            "--no-associate" => silent.associate = false,
            other => return Err(format!("unknown argument {other}")),
        }
    }
    if let Some(dir) = uninstall {
        return Ok(SetupCommand::Uninstall { dir, quiet });
    }
    if check_update {
        return Ok(SetupCommand::CheckUpdate { quiet });
    }
    match (out, from) {
        (Some(out), Some(from)) => Ok(SetupCommand::MakeInstaller { out, from, uninstaller }),
        (None, None) if is_silent => Ok(SetupCommand::Silent(silent)),
        (None, None) => Ok(SetupCommand::Wizard),
        _ => Err("--make-installer needs --from <folder>".into()),
    }
}

impl SetupSilent {
    pub fn to_args(&self) -> Vec<String> {
        let mut args = vec!["--silent".to_string()];
        if let Some(dir) = &self.dir {
            args.extend(["--dir".to_string(), dir.to_string_lossy().into_owned()]);
        }
        if let Some(file) = &self.result_file {
            args.extend(["--result-file".to_string(), file.to_string_lossy().into_owned()]);
        }
        for (on, flag) in [(self.per_user, "--user"), (!self.start_menu, "--no-start-menu"), (self.desktop, "--desktop"), (self.add_to_path, "--path"), (!self.associate, "--no-associate")] {
            if on {
                args.push(flag.to_string());
            }
        }
        args
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args(list: &[&str]) -> Vec<String> {
        list.iter().map(|s| s.to_string()).collect()
    }

    #[test]
    fn double_click_opens_the_wizard_with_the_image() {
        assert_eq!(
            parse_open(&args(&["C:\\x\\a.AppImage"])),
            Ok(OpenCommand::Wizard { image: Some(PathBuf::from("C:\\x\\a.AppImage")) })
        );
        assert_eq!(parse_open(&[]), Ok(OpenCommand::Wizard { image: None }));
    }

    #[test]
    fn silent_install_round_trips_through_arguments() {
        let original = OpenSilent {
            image: PathBuf::from("a b.AppImage"),
            name: Some("My App".into()),
            dir: Some(PathBuf::from("D:\\Apps\\My App")),
            per_user: true,
            start_menu: false,
            desktop: true,
            stub_missing: true,
            result_file: Some(PathBuf::from("r.txt")),
        };
        assert_eq!(parse_open(&original.to_args()), Ok(OpenCommand::Silent(original)));
    }

    #[test]
    fn open_errors_are_clear() {
        assert!(parse_open(&args(&["--silent"])).is_err());
        assert!(parse_open(&args(&["a", "b"])).is_err());
        assert!(parse_open(&args(&["--bogus"])).unwrap_err().contains("--bogus"));
        assert!(parse_open(&args(&["a", "--dir"])).unwrap_err().contains("needs a value"));
    }

    #[test]
    fn uninstall_wins_over_everything() {
        assert_eq!(
            parse_open(&args(&["--uninstall", "D:\\A", "--quiet"])),
            Ok(OpenCommand::Uninstall { dir: PathBuf::from("D:\\A"), quiet: true })
        );
        assert_eq!(
            parse_setup(&args(&["--uninstall", "D:\\C"])),
            Ok(SetupCommand::Uninstall { dir: PathBuf::from("D:\\C"), quiet: false })
        );
    }

    #[test]
    fn setup_defaults_associate_and_start_menu_on() {
        let Ok(SetupCommand::Silent(s)) = parse_setup(&args(&["--silent"])) else { panic!() };
        assert!(s.start_menu && s.associate && !s.desktop && !s.add_to_path && !s.per_user);
    }

    #[test]
    fn setup_options_round_trip() {
        let original = SetupSilent {
            dir: Some(PathBuf::from("D:\\C")),
            per_user: true,
            start_menu: false,
            desktop: true,
            add_to_path: true,
            associate: false,
            result_file: None,
        };
        assert_eq!(parse_setup(&original.to_args()), Ok(SetupCommand::Silent(original)));
    }

    #[test]
    fn checking_for_updates_is_always_asked_for() {
        assert_eq!(parse_open(&args(&["--check-update"])), Ok(OpenCommand::CheckUpdate { quiet: false }));
        assert_eq!(parse_setup(&args(&["--check-update", "--quiet"])), Ok(SetupCommand::CheckUpdate { quiet: true }));
        assert_eq!(parse_open(&[]), Ok(OpenCommand::Wizard { image: None }));
        assert!(matches!(parse_setup(&[]), Ok(SetupCommand::Wizard)));
    }

    #[test]
    fn make_installer_needs_both_paths() {
        assert!(matches!(parse_setup(&args(&["--make-installer", "o.exe", "--from", "d"])), Ok(SetupCommand::MakeInstaller { .. })));
        assert!(parse_setup(&args(&["--make-installer", "o.exe"])).is_err());
        assert_eq!(
            parse_setup(&args(&["--make-installer", "o.exe", "--from", "d", "--uninstaller", "u.exe"])),
            Ok(SetupCommand::MakeInstaller { out: PathBuf::from("o.exe"), from: PathBuf::from("d"), uninstaller: Some(PathBuf::from("u.exe")) })
        );
        assert_eq!(parse_setup(&[]), Ok(SetupCommand::Wizard));
    }
}
