// File: installer/src/command.rs
// Purpose: Build command lines: the `crossport convert` call, Windows argument quoting, shell verbs
// Author: Akilesh
// Date: 2026-11-05

use std::path::Path;

/// Quote one argument the way `CommandLineToArgvW` reads it back.
pub fn quote_arg(arg: &str) -> String {
    let needs_quotes = arg.is_empty() || arg.contains([' ', '\t', '"']);
    if !needs_quotes {
        return arg.to_string();
    }
    let mut out = String::from('"');
    let mut backslashes = 0;
    for c in arg.chars() {
        match c {
            '\\' => backslashes += 1,
            '"' => {
                out.push_str(&"\\".repeat(backslashes * 2 + 1));
                out.push('"');
                backslashes = 0;
            }
            _ => {
                out.push_str(&"\\".repeat(backslashes));
                out.push(c);
                backslashes = 0;
            }
        }
    }
    out.push_str(&"\\".repeat(backslashes * 2));
    out.push('"');
    out
}

/// A full command line: the program, then each argument quoted.
pub fn command_line(program: &Path, args: &[String]) -> String {
    let mut parts = vec![quote_arg(&program.to_string_lossy())];
    parts.extend(args.iter().map(|a| quote_arg(a)));
    parts.join(" ")
}

/// What `crossport convert` needs to run.
#[derive(Debug, Clone)]
pub struct ConvertJob<'a> {
    pub image: &'a Path,
    pub shim: &'a Path,
    pub out: &'a Path,
    pub runtime: Option<&'a Path>,
    pub stub_missing: bool,
}

impl ConvertJob<'_> {
    /// The arguments after the program name. Paths are passed whole (no quoting), as `Command` does that.
    pub fn args(&self) -> Vec<String> {
        let mut args = vec![
            "convert".to_string(),
            self.image.to_string_lossy().into_owned(),
            "--shim".to_string(),
            self.shim.to_string_lossy().into_owned(),
            "--out".to_string(),
            self.out.to_string_lossy().into_owned(),
        ];
        if let Some(runtime) = self.runtime {
            args.push("--runtime".to_string());
            args.push(runtime.to_string_lossy().into_owned());
        }
        if self.stub_missing {
            args.push("--stub-missing".to_string());
        }
        args
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn plain_arguments_stay_bare() {
        assert_eq!(quote_arg("abc"), "abc");
        assert_eq!(quote_arg("C:\\x\\y"), "C:\\x\\y");
    }

    #[test]
    fn spaces_and_empty_are_quoted() {
        assert_eq!(quote_arg("a b"), "\"a b\"");
        assert_eq!(quote_arg(""), "\"\"");
    }

    #[test]
    fn trailing_backslashes_are_doubled_inside_quotes() {
        assert_eq!(quote_arg("C:\\Program Files\\"), "\"C:\\Program Files\\\\\"");
    }

    #[test]
    fn quotes_are_escaped() {
        assert_eq!(quote_arg("say \"hi\""), "\"say \\\"hi\\\"\"");
        assert_eq!(quote_arg("a\\\"b c"), "\"a\\\\\\\"b c\"");
    }

    #[test]
    fn builds_the_shell_open_command() {
        let line = command_line(Path::new("C:\\Program Files\\Crossport\\crossport-open.exe"), &["%1".to_string()]);
        assert_eq!(line, "\"C:\\Program Files\\Crossport\\crossport-open.exe\" %1");
    }

    #[test]
    fn convert_arguments_follow_the_cli() {
        let (image, shim, out, runtime) = (PathBuf::from("a.AppImage"), PathBuf::from("s.dll"), PathBuf::from("o"), PathBuf::from("rt"));
        let job = ConvertJob { image: &image, shim: &shim, out: &out, runtime: Some(&runtime), stub_missing: true };
        assert_eq!(
            job.args(),
            ["convert", "a.AppImage", "--shim", "s.dll", "--out", "o", "--runtime", "rt", "--stub-missing"]
        );
        let bare = ConvertJob { runtime: None, stub_missing: false, ..job };
        assert_eq!(bare.args().len(), 6);
    }
}
