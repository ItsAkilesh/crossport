// File: tools/difftest/src/runner.rs
// Purpose: Run a command under controlled conditions (stdin, environment, directory, timeout) and record what it did
// Author: Akilesh
// Date: 2026-10-28

use std::fs::File;
use std::io::Read;
use std::path::PathBuf;
use std::process::{Child, Command, ExitStatus, Stdio};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use crate::error::DifftestError;
use crate::result::RunResult;
use crate::snapshot::snapshot;

pub const DEFAULT_TIMEOUT_SECS: u64 = 60;
pub const DEFAULT_MAX_OUTPUT_BYTES: usize = 64 * 1024 * 1024;
const POLL_INTERVAL: Duration = Duration::from_millis(10);
const READ_CHUNK_BYTES: usize = 8 * 1024;
/// Applied unless the caller overrides them, so locale and time zone never explain a difference.
const DETERMINISTIC_ENV: [(&str, &str); 3] = [("LANG", "C"), ("LC_ALL", "C"), ("TZ", "UTC")];

/// Everything that defines one run.
#[derive(Debug, Clone)]
pub struct RunSpec {
    pub command: Vec<String>,
    pub stdin: Option<PathBuf>,
    pub cwd: Option<PathBuf>,
    pub env: Vec<(String, String)>,
    pub timeout: Duration,
    /// Directory whose files are recorded after the run.
    pub capture_dir: Option<PathBuf>,
    /// Stdout and stderr are each capped at this many bytes; exceeding it is an error.
    pub max_output_bytes: usize,
}

impl RunSpec {
    pub fn new(command: Vec<String>) -> Self {
        Self {
            command,
            stdin: None,
            cwd: None,
            env: Vec::new(),
            timeout: Duration::from_secs(DEFAULT_TIMEOUT_SECS),
            capture_dir: None,
            max_output_bytes: DEFAULT_MAX_OUTPUT_BYTES,
        }
    }
}

struct Captured {
    bytes: Vec<u8>,
    was_truncated: bool,
}

fn io_error(context: String, source: std::io::Error) -> DifftestError {
    DifftestError::Io { context, source }
}

pub fn run(spec: &RunSpec) -> Result<RunResult, DifftestError> {
    let mut child = spawn(spec)?;
    let stdout = reader(child.stdout.take(), spec.max_output_bytes);
    let stderr = reader(child.stderr.take(), spec.max_output_bytes);
    let (status, timed_out) = wait(&mut child, spec.timeout)?;
    let (stdout, stderr) = (collect(stdout)?, collect(stderr)?);
    if stdout.was_truncated || stderr.was_truncated {
        return Err(DifftestError::Limit(format!(
            "output exceeded {} bytes",
            spec.max_output_bytes
        )));
    }
    let files = match &spec.capture_dir {
        Some(directory) => snapshot(directory)?,
        None => Default::default(),
    };
    Ok(RunResult {
        command: spec.command.clone(),
        exit_code: status.and_then(|s| s.code()),
        stdout: String::from_utf8_lossy(&stdout.bytes).into_owned(),
        stderr: String::from_utf8_lossy(&stderr.bytes).into_owned(),
        files,
        timed_out,
    })
}

fn spawn(spec: &RunSpec) -> Result<Child, DifftestError> {
    let (program, args) = spec
        .command
        .split_first()
        .ok_or_else(|| DifftestError::Usage("no command given to run".to_string()))?;
    let mut command = Command::new(program);
    command.args(args);
    for (key, value) in DETERMINISTIC_ENV {
        command.env(key, value);
    }
    command.envs(spec.env.iter().map(|(k, v)| (k, v)));
    if let Some(directory) = &spec.cwd {
        command.current_dir(directory);
    }
    command.stdin(match &spec.stdin {
        Some(path) => Stdio::from(
            File::open(path)
                .map_err(|e| io_error(format!("opening stdin file {}", path.display()), e))?,
        ),
        None => Stdio::null(),
    });
    command.stdout(Stdio::piped()).stderr(Stdio::piped());
    command
        .spawn()
        .map_err(|source| io_error(format!("running {program}"), source))
}

/// Drains a pipe on its own thread so a chatty child can never block on a full pipe.
fn reader<R: Read + Send + 'static>(pipe: Option<R>, cap: usize) -> Option<JoinHandle<Captured>> {
    pipe.map(|mut pipe| thread::spawn(move || read_capped(&mut pipe, cap)))
}

fn read_capped(pipe: &mut impl Read, cap: usize) -> Captured {
    let mut bytes = Vec::new();
    let mut was_truncated = false;
    let mut chunk = vec![0u8; READ_CHUNK_BYTES];
    while let Ok(count) = pipe.read(&mut chunk) {
        if count == 0 {
            break;
        }
        let room = cap.saturating_sub(bytes.len());
        bytes.extend_from_slice(&chunk[..count.min(room)]);
        was_truncated |= count > room;
    }
    Captured {
        bytes,
        was_truncated,
    }
}

fn collect(handle: Option<JoinHandle<Captured>>) -> Result<Captured, DifftestError> {
    match handle {
        Some(handle) => handle
            .join()
            .map_err(|_| DifftestError::Usage("an output reader thread panicked".to_string())),
        None => Ok(Captured {
            bytes: Vec::new(),
            was_truncated: false,
        }),
    }
}

/// Waits for the child, killing it when `timeout` passes. Returns the status
/// (absent after a kill) and whether the time limit was hit.
fn wait(child: &mut Child, timeout: Duration) -> Result<(Option<ExitStatus>, bool), DifftestError> {
    let deadline = Instant::now() + timeout;
    loop {
        if let Some(status) = child
            .try_wait()
            .map_err(|e| io_error("waiting for the program".to_string(), e))?
        {
            return Ok((Some(status), false));
        }
        if Instant::now() >= deadline {
            let _ = child.kill();
            let _ = child.wait();
            return Ok((None, true));
        }
        thread::sleep(POLL_INTERVAL);
    }
}
