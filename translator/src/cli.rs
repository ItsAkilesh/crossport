// File: translator/src/cli.rs
// Purpose: Parse command-line arguments into a typed Action
// Author: Akilesh
// Date: 2026-10-29

use std::path::PathBuf;

use crate::error::TranslateError;

const USAGE: &str = "usage:\n  crossport unpack <app.AppImage> --out <new-or-empty-dir>\n  crossport analyze <elf>\n  crossport coverage --shim <crossport_libc.dll> [--appdir <dir>] [--lib-dir <dir>]... [--allow-missing] <elf>...
  crossport translate <elf> --shim <crossport_libc.dll> --out <dir> [--lib-dir <dir>]... [--appdir <dir>]
  crossport convert <app.AppImage> --shim <crossport_libc.dll> --out <new-or-empty-dir> [--runtime <dir>]...";

#[derive(Debug, PartialEq, Eq)]
pub enum Action {
    Unpack {
        image: PathBuf,
        out: PathBuf,
    },
    Analyze {
        elf: PathBuf,
    },
    Coverage {
        shim: PathBuf,
        elfs: Vec<PathBuf>,
        lib_dirs: Vec<PathBuf>,
        appdir: Option<PathBuf>,
        /// Measure even when some libraries are missing (host libraries the AppDir does not bundle).
        allow_missing: bool,
    },
    /// Unpack an AppImage and translate it into a runnable bundle in one step.
    Convert {
        image: PathBuf,
        shim: PathBuf,
        out: PathBuf,
        /// Directories of libraries the AppImage expects from its host (libstdc++ and the like).
        runtime_dirs: Vec<PathBuf>,
    },
    Translate {
        elf: PathBuf,
        shim: PathBuf,
        out: PathBuf,
        lib_dirs: Vec<PathBuf>,
        /// Search libraries inside this unpacked AppDir (RUNPATH, $ORIGIN, default directories).
        appdir: Option<PathBuf>,
    },
}

pub fn parse(args: &[String]) -> Result<Action, TranslateError> {
    match args {
        [command, image, flag, out] if command == "unpack" && flag == "--out" => {
            Ok(Action::Unpack {
                image: PathBuf::from(image),
                out: PathBuf::from(out),
            })
        }
        [command, elf] if command == "analyze" => Ok(Action::Analyze {
            elf: PathBuf::from(elf),
        }),
        [command, elf, flags @ ..] if command == "translate" => parse_translate(elf, flags),
        [command, rest @ ..] if command == "coverage" => parse_coverage(rest),
        [command, image, flags @ ..] if command == "convert" => parse_convert(image, flags),
        _ => Err(usage()),
    }
}

fn parse_convert(image: &str, flags: &[String]) -> Result<Action, TranslateError> {
    let (mut shim, mut out) = (None, None);
    let mut runtime_dirs = Vec::new();
    let mut remaining = flags.iter();
    while let Some(flag) = remaining.next() {
        let value = PathBuf::from(remaining.next().ok_or_else(usage)?);
        match flag.as_str() {
            "--shim" if shim.is_none() => shim = Some(value),
            "--out" if out.is_none() => out = Some(value),
            "--runtime" => runtime_dirs.push(value),
            _ => return Err(usage()),
        }
    }
    match (shim, out) {
        (Some(shim), Some(out)) => Ok(Action::Convert {
            image: PathBuf::from(image),
            shim,
            out,
            runtime_dirs,
        }),
        _ => Err(usage()),
    }
}

fn parse_translate(elf: &str, flags: &[String]) -> Result<Action, TranslateError> {
    let mut shim = None;
    let mut out = None;
    let mut lib_dirs = Vec::new();
    let mut appdir = None;
    let mut remaining = flags.iter();
    while let Some(flag) = remaining.next() {
        let value = PathBuf::from(remaining.next().ok_or_else(usage)?);
        match flag.as_str() {
            "--shim" if shim.is_none() => shim = Some(value),
            "--out" if out.is_none() => out = Some(value),
            "--lib-dir" => lib_dirs.push(value),
            "--appdir" if appdir.is_none() => appdir = Some(value),
            _ => return Err(usage()),
        }
    }
    match (shim, out) {
        (Some(shim), Some(out)) => Ok(Action::Translate {
            elf: PathBuf::from(elf),
            shim,
            out,
            lib_dirs,
            appdir,
        }),
        _ => Err(usage()),
    }
}

fn parse_coverage(args: &[String]) -> Result<Action, TranslateError> {
    let (mut shim, mut appdir) = (None, None);
    let (mut elfs, mut lib_dirs) = (Vec::new(), Vec::new());
    let mut allow_missing = false;
    let mut remaining = args.iter();
    while let Some(argument) = remaining.next() {
        match argument.as_str() {
            "--shim" if shim.is_none() => {
                shim = Some(PathBuf::from(remaining.next().ok_or_else(usage)?))
            }
            "--appdir" if appdir.is_none() => {
                appdir = Some(PathBuf::from(remaining.next().ok_or_else(usage)?))
            }
            "--lib-dir" => lib_dirs.push(PathBuf::from(remaining.next().ok_or_else(usage)?)),
            "--allow-missing" => allow_missing = true,
            flag if flag.starts_with("--") => return Err(usage()),
            elf => elfs.push(PathBuf::from(elf)),
        }
    }
    match shim {
        Some(shim) if !elfs.is_empty() => Ok(Action::Coverage {
            shim,
            elfs,
            lib_dirs,
            appdir,
            allow_missing,
        }),
        _ => Err(usage()),
    }
}

fn usage() -> TranslateError {
    TranslateError::Usage(USAGE.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args(items: &[&str]) -> Vec<String> {
        items.iter().map(|item| item.to_string()).collect()
    }

    #[test]
    fn parses_convert_with_runtime_directories() {
        let action = parse(&args(&[
            "convert",
            "a.AppImage",
            "--shim",
            "s.dll",
            "--out",
            "o",
            "--runtime",
            "r1",
            "--runtime",
            "r2",
        ]));
        assert_eq!(
            action.expect("valid"),
            Action::Convert {
                image: PathBuf::from("a.AppImage"),
                shim: PathBuf::from("s.dll"),
                out: PathBuf::from("o"),
                runtime_dirs: vec![PathBuf::from("r1"), PathBuf::from("r2")],
            }
        );
        assert!(
            parse(&args(&["convert", "a", "--out", "o"])).is_err(),
            "needs a shim"
        );
        assert!(parse(&args(&["convert", "a", "--shim", "s", "--bogus", "x"])).is_err());
    }

    #[test]
    fn parses_unpack() {
        assert_eq!(
            parse(&args(&["unpack", "a.AppImage", "--out", "dir"])).expect("valid"),
            Action::Unpack {
                image: PathBuf::from("a.AppImage"),
                out: PathBuf::from("dir")
            }
        );
        assert!(parse(&args(&["unpack", "a.AppImage"])).is_err());
        assert!(parse(&args(&["unpack", "a.AppImage", "--to", "dir"])).is_err());
    }

    #[test]
    fn parses_coverage_with_several_programs_and_options_in_any_position() {
        let action = parse(&args(&[
            "coverage",
            "a",
            "--shim",
            "s.dll",
            "b",
            "--lib-dir",
            "l",
            "--appdir",
            "app",
        ]));
        assert_eq!(
            action.expect("valid"),
            Action::Coverage {
                shim: PathBuf::from("s.dll"),
                elfs: vec![PathBuf::from("a"), PathBuf::from("b")],
                lib_dirs: vec![PathBuf::from("l")],
                appdir: Some(PathBuf::from("app")),
                allow_missing: false,
            }
        );
        assert!(
            parse(&args(&["coverage", "--shim", "s.dll"])).is_err(),
            "needs a program"
        );
        assert!(parse(&args(&["coverage", "a"])).is_err(), "needs a shim");
        assert!(parse(&args(&["coverage", "a", "--shim", "s", "--bogus"])).is_err());
    }

    #[test]
    fn parses_analyze() {
        assert_eq!(
            parse(&args(&["analyze", "a.out"])).expect("valid"),
            Action::Analyze {
                elf: PathBuf::from("a.out")
            }
        );
    }

    #[test]
    fn parses_translate_with_repeated_library_directories_in_any_order() {
        let action = parse(&args(&[
            "translate",
            "a.out",
            "--lib-dir",
            "l1",
            "--out",
            "dist",
            "--shim",
            "s.dll",
            "--lib-dir",
            "l2",
        ]));
        assert_eq!(
            action.expect("valid"),
            Action::Translate {
                elf: PathBuf::from("a.out"),
                shim: PathBuf::from("s.dll"),
                out: PathBuf::from("dist"),
                lib_dirs: vec![PathBuf::from("l1"), PathBuf::from("l2")],
                appdir: None,
            }
        );
    }

    #[test]
    fn parses_the_appdir_option() {
        let action = parse(&args(&[
            "translate",
            "app/usr/bin/tool",
            "--shim",
            "s.dll",
            "--out",
            "dist",
            "--appdir",
            "app",
        ]));
        assert_eq!(
            action.expect("valid"),
            Action::Translate {
                elf: PathBuf::from("app/usr/bin/tool"),
                shim: PathBuf::from("s.dll"),
                out: PathBuf::from("dist"),
                lib_dirs: vec![],
                appdir: Some(PathBuf::from("app")),
            }
        );
        assert!(parse(&args(&[
            "translate",
            "t",
            "--shim",
            "s",
            "--out",
            "d",
            "--appdir",
            "a",
            "--appdir",
            "b"
        ]))
        .is_err());
    }

    #[test]
    fn rejects_incomplete_duplicate_or_unknown_flags() {
        assert!(parse(&[]).is_err());
        assert!(parse(&args(&["translate", "a.out"])).is_err());
        assert!(parse(&args(&["translate", "a.out", "--shim", "s", "--out"])).is_err());
        assert!(parse(&args(&[
            "translate",
            "a.out",
            "--shim",
            "s",
            "--shim",
            "t",
            "--out",
            "d"
        ]))
        .is_err());
        assert!(parse(&args(&[
            "translate",
            "a.out",
            "--shim",
            "s",
            "--out",
            "d",
            "--bogus",
            "x"
        ]))
        .is_err());
        assert!(parse(&args(&["frobnicate", "x"])).is_err());
    }
}
