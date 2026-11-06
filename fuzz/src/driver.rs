// File: fuzz/src/driver.rs
// Purpose: Stable-Rust mutation fuzzer over the fuzz targets, with hang and big-allocation detection; for local runs
// Author: Akilesh
// Date: 2026-11-05

use std::alloc::{GlobalAlloc, Layout, System};
use std::fs;
use std::panic::{self, AssertUnwindSafe};
use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::sync::atomic::{AtomicBool, AtomicPtr, AtomicU64, AtomicUsize, Ordering};
use std::time::{Duration, Instant};

use crossport_fuzz::TARGETS;

/// One allocation above this is reported as unbounded (the translator caps every input far below it).
const MAX_ALLOCATION: usize = 1 << 30;
const HANG_SECONDS: u64 = 20;
const MAX_INPUT: usize = 4 * 1024 * 1024;
const MAX_MUTATIONS: usize = 6;
const HEADER_BIAS_BYTES: usize = 1024;
const CAUGHT_PANIC_SOURCE: &str = "backhand-";
const DEFAULT_SECONDS: u64 = 60;
const DEFAULT_SEED: u64 = 0x9e37_79b9_7f4a_7c15;
const INTERESTING: [u64; 12] = [
    0,
    1,
    0x7f,
    0x80,
    0xff,
    0x100,
    0x7fff,
    0xffff,
    0x7fff_ffff,
    0xffff_ffff,
    0x7fff_ffff_ffff_ffff,
    u64::MAX,
];

struct Guard;

static CURRENT_PTR: AtomicPtr<u8> = AtomicPtr::new(std::ptr::null_mut());
static CURRENT_LEN: AtomicUsize = AtomicUsize::new(0);
static CURRENT_TARGET: AtomicPtr<u8> = AtomicPtr::new(std::ptr::null_mut());
static CURRENT_TARGET_LEN: AtomicUsize = AtomicUsize::new(0);
static STARTED_MILLIS: AtomicU64 = AtomicU64::new(0);
static REPORTING: AtomicBool = AtomicBool::new(false);
static ARTIFACTS: AtomicPtr<String> = AtomicPtr::new(std::ptr::null_mut());

// SAFETY: every method forwards to the system allocator unchanged; the size check only reports and aborts.
unsafe impl GlobalAlloc for Guard {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if layout.size() > MAX_ALLOCATION {
            record_failure("alloc");
        }
        System.alloc(layout)
    }
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        System.dealloc(ptr, layout)
    }
    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        if new_size > MAX_ALLOCATION {
            record_failure("alloc");
        }
        System.realloc(ptr, layout, new_size)
    }
}

#[global_allocator]
static ALLOCATOR: Guard = Guard;

/// Saves the input that is running as `<artifacts>/<target>/<kind>-<hash>` and ends the process.
fn record_failure(kind: &str) -> ! {
    if !REPORTING.swap(true, Ordering::SeqCst) {
        let ptr = CURRENT_PTR.load(Ordering::SeqCst);
        let len = CURRENT_LEN.load(Ordering::SeqCst);
        let target_ptr = CURRENT_TARGET.load(Ordering::SeqCst);
        let target_len = CURRENT_TARGET_LEN.load(Ordering::SeqCst);
        let root = ARTIFACTS.load(Ordering::SeqCst);
        if !ptr.is_null() && !target_ptr.is_null() && !root.is_null() {
            // SAFETY: the pointers refer to buffers the main thread keeps alive while a case runs.
            let (input, target, root) = unsafe {
                (
                    std::slice::from_raw_parts(ptr, len),
                    String::from_utf8_lossy(std::slice::from_raw_parts(target_ptr, target_len))
                        .into_owned(),
                    &*root,
                )
            };
            let dir = Path::new(root).join(target.as_str());
            let _ = fs::create_dir_all(&dir);
            let name = format!("{kind}-{:016x}", hash(input));
            let _ = fs::write(dir.join(&name), input);
            eprintln!(
                "FAILURE {kind} in {target}: saved {}",
                dir.join(name).display()
            );
        }
    }
    std::process::abort()
}

fn hash(bytes: &[u8]) -> u64 {
    bytes.iter().fold(0xcbf2_9ce4_8422_2325u64, |h, b| {
        (h ^ u64::from(*b)).wrapping_mul(0x100_0000_01b3)
    })
}

struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0
    }

    /// A position in `len` bytes, biased towards the start where ELF and SquashFS headers and tables live.
    fn position(&mut self, len: usize) -> usize {
        if self.below(2) == 0 {
            self.below(len.min(HEADER_BIAS_BYTES))
        } else {
            self.below(len)
        }
    }

    fn below(&mut self, bound: usize) -> usize {
        (self.next() % bound.max(1) as u64) as usize
    }
}

fn overwrite_integer(input: &mut [u8], rng: &mut Rng) {
    let value = INTERESTING[rng.below(INTERESTING.len())].to_le_bytes();
    let width = [1, 2, 4, 8][rng.below(4)];
    let at = rng.position(input.len());
    for (offset, byte) in value.iter().take(width).enumerate() {
        if let Some(slot) = input.get_mut(at + offset) {
            *slot = *byte;
        }
    }
}

fn mutate_once(input: &mut Vec<u8>, other: &[u8], rng: &mut Rng) {
    let len = input.len();
    match rng.below(9) {
        0 if len > 0 => input[rng.position(len)] ^= 1 << rng.below(8),
        1 if len > 0 => input[rng.position(len)] = rng.next() as u8,
        2 if len > 0 => overwrite_integer(input, rng),
        3 if len > 1 => input.truncate(rng.below(len).max(1)),
        4 if len > 0 => {
            let from = rng.below(len);
            let chunk: Vec<u8> = input[from..len.min(from + 1 + rng.below(64))].to_vec();
            let at = rng.below(len);
            input.splice(at..at, chunk);
        }
        5 if len > 0 => {
            let from = rng.below(len);
            input.drain(from..len.min(from + 1 + rng.below(64)));
        }
        6 if !other.is_empty() => {
            let from = rng.below(other.len());
            let chunk = &other[from..other.len().min(from + 1 + rng.below(256))];
            let at = rng.below(len + 1);
            input.splice(at..at, chunk.iter().copied());
        }
        7 if len > 0 => {
            let at = rng.position(len);
            input[at] = input[at].wrapping_add((rng.below(7) as i8 - 3) as u8);
        }
        _ => input.push(rng.next() as u8),
    }
}

fn mutate(input: &mut Vec<u8>, other: &[u8], rng: &mut Rng) {
    for _ in 0..=rng.below(MAX_MUTATIONS) {
        mutate_once(input, other, rng);
    }
    input.truncate(MAX_INPUT);
}

fn load_corpus(dir: &Path) -> Vec<Vec<u8>> {
    let mut seeds: Vec<Vec<u8>> = fs::read_dir(dir)
        .into_iter()
        .flatten()
        .flatten()
        .filter_map(|entry| fs::read(entry.path()).ok())
        .collect();
    if seeds.is_empty() {
        seeds.push(vec![0]);
    }
    seeds
}

fn start_watchdog(started: Instant) {
    std::thread::spawn(move || loop {
        std::thread::sleep(Duration::from_secs(1));
        let began = STARTED_MILLIS.load(Ordering::SeqCst);
        let now = started.elapsed().as_millis() as u64;
        if began != 0 && now.saturating_sub(began) > HANG_SECONDS * 1000 {
            record_failure("hang");
        }
    });
}

fn run_one(name: &'static str, target: fn(&[u8]), input: &[u8], started: Instant) {
    CURRENT_PTR.store(input.as_ptr() as *mut u8, Ordering::SeqCst);
    CURRENT_LEN.store(input.len(), Ordering::SeqCst);
    CURRENT_TARGET.store(name.as_ptr() as *mut u8, Ordering::SeqCst);
    CURRENT_TARGET_LEN.store(name.len(), Ordering::SeqCst);
    STARTED_MILLIS.store(
        (started.elapsed().as_millis() as u64).max(1),
        Ordering::SeqCst,
    );
    let _ = panic::catch_unwind(AssertUnwindSafe(|| target(input)));
    STARTED_MILLIS.store(0, Ordering::SeqCst);
}

struct Options {
    target: String,
    corpus: PathBuf,
    artifacts: PathBuf,
    seconds: u64,
    seed: u64,
}

fn parse_options() -> Option<Options> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let [target, rest @ ..] = args.as_slice() else {
        return None;
    };
    let mut options = Options {
        target: target.clone(),
        corpus: PathBuf::from("corpus").join(target),
        artifacts: PathBuf::from("artifacts"),
        seconds: DEFAULT_SECONDS,
        seed: DEFAULT_SEED,
    };
    for pair in rest.chunks(2) {
        match pair {
            [flag, value] if flag == "--seconds" => options.seconds = value.parse().ok()?,
            [flag, value] if flag == "--seed" => options.seed = value.parse().ok()?,
            [flag, value] if flag == "--corpus" => options.corpus = PathBuf::from(value),
            [flag, value] if flag == "--artifacts" => options.artifacts = PathBuf::from(value),
            _ => return None,
        }
    }
    Some(options)
}

fn main() -> ExitCode {
    let Some(options) = parse_options() else {
        eprintln!(
            "usage: driver <target> [--seconds N] [--seed N] [--corpus DIR] [--artifacts DIR]"
        );
        eprintln!("targets: {}", TARGETS.map(|(name, _)| name).join(" "));
        return ExitCode::from(2);
    };
    let Some(&(name, target)) = TARGETS.iter().find(|(name, _)| *name == options.target) else {
        eprintln!("unknown target {}", options.target);
        return ExitCode::from(2);
    };
    let artifacts = Box::new(options.artifacts.to_string_lossy().into_owned());
    ARTIFACTS.store(Box::into_raw(artifacts), Ordering::SeqCst);
    panic::set_hook(Box::new(|info| {
        // `unpack` catches the SquashFS reader's own panics on purpose; only the translator's count as crashes.
        if info
            .location()
            .is_some_and(|at| at.file().contains(CAUGHT_PANIC_SOURCE))
        {
            return;
        }
        eprintln!("panic: {info}");
        record_failure("crash");
    }));
    let started = Instant::now();
    start_watchdog(started);
    let seeds = load_corpus(&options.corpus);
    let mut rng = Rng(options.seed | 1);
    let mut executions = 0u64;
    for seed in &seeds {
        run_one(name, target, seed, started);
        executions += 1;
    }
    let deadline = Duration::from_secs(options.seconds);
    while started.elapsed() < deadline {
        let mut input = seeds[rng.below(seeds.len())].clone();
        let other = &seeds[rng.below(seeds.len())];
        mutate(&mut input, other, &mut rng);
        run_one(name, target, &input, started);
        executions += 1;
    }
    println!(
        "{name}: {executions} executions in {}s, no failure",
        options.seconds
    );
    ExitCode::SUCCESS
}
