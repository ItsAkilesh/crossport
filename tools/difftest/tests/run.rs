// File: tools/difftest/tests/run.rs
// Purpose: Integration tests of the runner against the probe program: arguments, stdin, environment, directory, files, timeout, limits
// Author: Akilesh
// Date: 2026-10-28

use std::fs;
use std::path::PathBuf;
use std::time::{Duration, Instant};

use difftest::error::DifftestError;
use difftest::runner::{run, RunSpec};

const PROBE: &str = env!("CARGO_BIN_EXE_difftest-probe");

fn spec(arguments: &[&str]) -> RunSpec {
    let mut command = vec![PROBE.to_string()];
    command.extend(arguments.iter().map(|a| a.to_string()));
    RunSpec::new(command)
}

fn scratch(name: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("difftest-run-{name}"));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).expect("temp dir");
    dir
}

#[test]
fn arguments_arrive_intact_including_awkward_ones() {
    let result = run(&spec(&["args", "plain", "two words", "--flag=value", ""])).expect("runs");
    assert_eq!(
        result.stdout.lines().collect::<Vec<_>>(),
        ["plain", "two words", "--flag=value", ""]
    );
    assert_eq!(result.exit_code, Some(0));
}

#[test]
fn the_exit_code_and_stderr_are_recorded() {
    let result = run(&spec(&["exit", "7"])).expect("runs");
    assert_eq!(result.exit_code, Some(7));
    let result = run(&spec(&["stderr", "oh", "no"])).expect("runs");
    assert_eq!(result.stderr, "oh no\n");
}

#[test]
fn stdin_comes_from_the_named_file_and_is_empty_otherwise() {
    let dir = scratch("stdin");
    fs::write(dir.join("in.txt"), b"line one\nline two\n").expect("file");
    let mut with_stdin = spec(&["stdin"]);
    with_stdin.stdin = Some(dir.join("in.txt"));
    assert_eq!(
        run(&with_stdin).expect("runs").stdout,
        "line one\nline two\n"
    );
    assert_eq!(run(&spec(&["stdin"])).expect("runs").stdout, "");
}

#[test]
fn the_environment_is_deterministic_by_default_and_overridable() {
    let defaults = run(&spec(&["env", "LANG", "LC_ALL", "TZ"])).expect("runs");
    assert_eq!(
        defaults.stdout.lines().collect::<Vec<_>>(),
        ["C", "C", "UTC"]
    );
    let mut overridden = spec(&["env", "LC_ALL", "CUSTOM"]);
    overridden.env = vec![
        ("LC_ALL".into(), "en_US.UTF-8".into()),
        ("CUSTOM".into(), "a=b".into()),
    ];
    assert_eq!(
        run(&overridden)
            .expect("runs")
            .stdout
            .lines()
            .collect::<Vec<_>>(),
        ["en_US.UTF-8", "a=b"]
    );
}

#[test]
fn the_working_directory_is_honoured() {
    let dir = scratch("cwd");
    let mut in_dir = spec(&["cwd"]);
    in_dir.cwd = Some(dir.clone());
    let printed = run(&in_dir).expect("runs").stdout;
    assert!(
        printed.trim_end().ends_with("difftest-run-cwd"),
        "{printed}"
    );
}

#[test]
fn files_written_under_the_capture_directory_are_recorded_with_hashes() {
    let dir = scratch("capture");
    let mut writing = spec(&["write", "out.txt", "abc"]);
    writing.cwd = Some(dir.clone());
    writing.capture_dir = Some(dir);
    let result = run(&writing).expect("runs");
    assert_eq!(result.files["out.txt"].size, 3);
    assert_eq!(
        result.files["out.txt"].sha256,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    );
}

#[test]
fn a_program_that_overruns_its_time_limit_is_killed_and_marked() {
    let mut slow = spec(&["sleep", "30000"]);
    slow.timeout = Duration::from_millis(300);
    let started = Instant::now();
    let result = run(&slow).expect("runs");
    assert!(result.timed_out);
    assert_eq!(result.exit_code, None);
    assert!(
        started.elapsed() < Duration::from_secs(10),
        "it was killed, not waited for"
    );
}

#[test]
fn output_beyond_the_cap_is_an_error_not_unbounded_memory() {
    let mut chatty = spec(&["flood", "200"]);
    chatty.max_output_bytes = 4096;
    assert!(matches!(run(&chatty), Err(DifftestError::Limit(_))));
}

#[test]
fn a_missing_program_or_stdin_file_is_a_typed_error() {
    let mut missing = RunSpec::new(vec!["difftest-no-such-program".to_string()]);
    assert!(matches!(run(&missing), Err(DifftestError::Io { .. })));
    missing = spec(&["stdin"]);
    missing.stdin = Some(std::env::temp_dir().join("difftest-no-such-input"));
    assert!(matches!(run(&missing), Err(DifftestError::Io { .. })));
    assert!(matches!(
        run(&RunSpec::new(Vec::new())),
        Err(DifftestError::Usage(_))
    ));
}
