// File: installer/src/report.rs
// Purpose: Turn the output of `crossport convert` into a readable report of what is unsupported
// Author: Akilesh
// Date: 2026-11-05

const MISSING_LIBRARY_MARK: &str = "library ";
const MISSING_LIBRARY_TAIL: &str = " was not found";
const LEFT_OUT_MARK: &str = "not found, left out: ";
const SYSCALL_MARK: &str = "raw syscall instructions";
const SYMBOLS_MARK: &str = "symbols are not defined by any bundled library or the shim";
const MAX_TAIL_LINES: usize = 8;

/// A finding the user should know about, with advice.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Finding {
    pub summary: String,
    pub advice: String,
}

/// The result of one conversion, as shown on the last wizard page.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Report {
    pub succeeded: bool,
    pub findings: Vec<Finding>,
}

fn finding(summary: String, advice: &str) -> Finding {
    Finding { summary, advice: advice.to_string() }
}

/// Read the translator's combined output (stdout then stderr) and its exit status.
pub fn analyze(output: &str, exit_ok: bool) -> Report {
    let mut findings: Vec<Finding> = output.lines().map(str::trim).filter_map(classify).collect();
    if !exit_ok && findings.is_empty() {
        findings.push(unknown_failure(output));
    }
    Report { succeeded: exit_ok, findings }
}

fn classify(line: &str) -> Option<Finding> {
    if let Some(name) = line.strip_prefix(LEFT_OUT_MARK) {
        return Some(finding(
            format!("The library {name} is not provided and was left out."),
            "The app may still run if it never uses it. The Crossport runtime pack supplies common libraries.",
        ));
    }
    if line.contains(MISSING_LIBRARY_MARK) && line.contains(MISSING_LIBRARY_TAIL) {
        let name = line.split(MISSING_LIBRARY_MARK).nth(1)?.split(MISSING_LIBRARY_TAIL).next()?;
        return Some(finding(
            format!("The app needs the library {name}, which Crossport does not have."),
            "This app expects a Linux library that it does not bundle. Reinstall Crossport with the runtime pack, or try again with missing libraries stubbed.",
        ));
    }
    if line.contains(SYMBOLS_MARK) {
        return Some(finding(
            line.trim_start_matches("crossport: ").to_string(),
            "Crossport does not implement some system functions this app uses yet. Please report the list.",
        ));
    }
    if line.contains(SYSCALL_MARK) {
        return Some(finding(
            line.trim_start_matches("warning: ").to_string(),
            "The app makes direct Linux system calls, which Crossport cannot translate yet. It may misbehave if it reaches them.",
        ));
    }
    if line.contains("only position-independent executables") {
        return Some(finding(
            line.trim_start_matches("crossport: ").to_string(),
            "This app's program was built as a fixed-address executable (not position independent). Crossport can only translate position-independent programs for now.",
        ));
    }
    if line.contains("unsupported:") || line.contains("malformed ELF") {
        return Some(finding(
            line.trim_start_matches("crossport: ").to_string(),
            "This file is not something Crossport can translate (it must be an x86-64 type 2 AppImage).",
        ));
    }
    None
}

fn unknown_failure(output: &str) -> Finding {
    let lines: Vec<&str> = output.lines().filter(|l| !l.trim().is_empty()).collect();
    let start = lines.len().saturating_sub(MAX_TAIL_LINES);
    finding(
        format!("The conversion failed.\n{}", lines[start..].join("\n")),
        "Please include this text in a bug report.",
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_clean_run_has_no_findings() {
        let report = analyze("program: usr/bin/lua\nwrote C:/x/lua.exe (3 modules)\n", true);
        assert!(report.succeeded && report.findings.is_empty());
    }

    #[test]
    fn explains_a_missing_library() {
        let report = analyze("crossport: library libstdc++.so.6 was not found in any --lib-dir\n", false);
        assert!(!report.succeeded);
        assert!(report.findings[0].summary.contains("libstdc++.so.6"));
    }

    #[test]
    fn lists_left_out_libraries_even_on_success() {
        let report = analyze("not found, left out: libasound.so.2\nwrote x\n", true);
        assert!(report.succeeded);
        assert!(report.findings[0].summary.contains("libasound.so.2"));
    }

    #[test]
    fn explains_raw_syscalls_and_missing_symbols() {
        let text = "  warning: app has 3 raw syscall instructions, which are not translated\ncrossport: 2 symbols are not defined by any bundled library or the shim: a, b\n";
        let report = analyze(text, false);
        assert_eq!(report.findings.len(), 2);
        assert!(report.findings[0].summary.starts_with("app has 3 raw syscall"));
        assert!(report.findings[1].summary.contains("2 symbols"));
    }

    #[test]
    fn explains_a_non_pie_program_as_seen_with_vlc() {
        let text = "program: usr/bin/vlc
crossport: unsupported: only position-independent executables and shared objects (ET_DYN) are supported
";
        let report = analyze(text, false);
        assert_eq!(report.findings.len(), 1);
        assert!(report.findings[0].advice.contains("fixed-address"));
    }

    #[test]
    fn unknown_failures_show_the_tail_of_the_output() {
        let text: String = (0..20).map(|i| format!("line {i}\n")).collect();
        let report = analyze(&text, false);
        let summary = &report.findings[0].summary;
        assert!(summary.contains("line 19") && !summary.contains("line 3\n"));
    }
}
