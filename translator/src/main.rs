// File: translator/src/main.rs
// Purpose: crossport command-line entry point; exit code 0 = success, 2 = error
// Author: Akilesh
// Date: 2026-10-29

use std::fs::File;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use crossport_translator::analyze::analyze;
use crossport_translator::appimage::{self, Limits};
use crossport_translator::cli::{self, Action};
use crossport_translator::error::TranslateError;
use crossport_translator::naming::ModuleName;
use crossport_translator::shim_exports::ShimExports;
use crossport_translator::source::{read_bounded, AppDirSource, DirectorySource, LibrarySource};
use crossport_translator::{bundle, coverage, graph, translate};

const EXIT_ERROR: u8 = 2;
const DEFAULT_PROGRAM_NAME: &str = "program";

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match cli::parse(&args).and_then(execute) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("crossport: {error}");
            ExitCode::from(EXIT_ERROR)
        }
    }
}

fn execute(action: Action) -> Result<(), TranslateError> {
    match action {
        Action::Unpack { image, out } => unpack_image(&image, &out),
        Action::Analyze { elf } => show_analysis(&elf),
        Action::Coverage {
            shim,
            elfs,
            lib_dirs,
            appdir,
            allow_missing,
        } => show_coverage(&shim, &elfs, &lib_dirs, appdir.as_deref(), allow_missing),
        Action::Translate {
            elf,
            shim,
            out,
            lib_dirs,
            appdir,
        } => run_translation(&elf, &shim, &out, lib_dirs, appdir),
    }
}

fn unpack_image(image: &Path, out: &Path) -> Result<(), TranslateError> {
    let app_dir = appimage::unpack(image, out, &Limits::default())?;
    let report = &app_dir.report;
    println!(
        "unpacked {} files and {} directories ({} bytes) to {}",
        report.files,
        report.directories,
        report.total_bytes,
        app_dir.root.display()
    );
    println!("AppRun runs {}", app_dir.apprun_target.join("/"));
    println!("{} symlinks became copies", report.links_materialized);
    for skipped in &report.skipped {
        println!("skipped {skipped}");
    }
    Ok(())
}

fn show_coverage(
    shim: &Path,
    elfs: &[PathBuf],
    lib_dirs: &[PathBuf],
    appdir: Option<&Path>,
    allow_missing: bool,
) -> Result<(), TranslateError> {
    let exports = ShimExports::from_dll(&read_file(shim)?)?;
    let mut programs = Vec::with_capacity(elfs.len());
    let mut missing_libraries = Vec::new();
    for elf in elfs {
        let stem = elf
            .file_stem()
            .and_then(|s| s.to_str())
            .unwrap_or(DEFAULT_PROGRAM_NAME);
        let loaded = library_source(elf, appdir.map(Path::to_path_buf), lib_dirs.to_vec())
            .and_then(|source| {
                let (name, bytes) = (ModuleName::sanitized(stem)?, read_file(elf)?);
                if allow_missing {
                    graph::load_lenient(name, bytes, source.as_ref(), &mut missing_libraries)
                } else {
                    graph::load(name, bytes, source.as_ref())
                }
            });
        programs.push((stem.to_string(), loaded));
    }
    for library in &missing_libraries {
        println!("not found (left out of the measurement): {library}");
    }
    print_coverage(&coverage::measure(programs, &exports));
    Ok(())
}

fn print_coverage(coverage: &coverage::Coverage) {
    println!(
        "programs: {} loaded of {}",
        coverage.loaded_programs(),
        coverage.programs.len()
    );
    for program in &coverage.programs {
        match &program.result {
            Ok(modules) => println!("  ok      {} ({modules} modules)", program.name),
            Err(reason) => println!("  FAILED  {}: {reason}", program.name),
        }
    }
    let provided = coverage.symbols.iter().filter(|s| s.is_provided).count();
    println!(
        "symbols needed from the C library: {} ({provided} provided by the shim)",
        coverage.symbols.len()
    );
    if let Some(ratio) = coverage.weighted_coverage() {
        println!("usage-weighted coverage: {:.1}%", ratio * 100.0);
    }
    println!("{:>8}  {:<9} symbol", "programs", "status");
    for symbol in &coverage.symbols {
        let status = match (symbol.is_provided, symbol.is_weak_only) {
            (true, _) => "provided",
            (false, true) => "weak",
            (false, false) => "MISSING",
        };
        println!("{:>8}  {:<9} {}", symbol.programs, status, symbol.name);
    }
}

fn read_file(path: &Path) -> Result<Vec<u8>, TranslateError> {
    let file = File::open(path).map_err(|source| TranslateError::Io {
        context: format!("opening {}", path.display()),
        source,
    })?;
    read_bounded(file, &path.display().to_string())
}

/// An AppDir search when `--appdir` is given, otherwise a plain directory list that defaults to
/// the executable's own directory.
fn library_source(
    elf: &Path,
    appdir: Option<PathBuf>,
    mut lib_dirs: Vec<PathBuf>,
) -> Result<Box<dyn LibrarySource>, TranslateError> {
    let canonical = |path: &Path| {
        path.canonicalize().map_err(|source| TranslateError::Io {
            context: format!("resolving {}", path.display()),
            source,
        })
    };
    if let Some(root) = appdir {
        let extra = lib_dirs
            .iter()
            .map(|dir| canonical(dir))
            .collect::<Result<Vec<_>, _>>()?;
        let source = AppDirSource::new(canonical(&root)?, canonical(elf)?, &extra)?;
        return Ok(Box::new(source));
    }
    if lib_dirs.is_empty() {
        lib_dirs.push(
            elf.parent()
                .map_or_else(|| PathBuf::from("."), Path::to_path_buf),
        );
    }
    Ok(Box::new(DirectorySource::new(lib_dirs)))
}

fn run_translation(
    elf: &Path,
    shim: &Path,
    out: &Path,
    lib_dirs: Vec<PathBuf>,
    appdir: Option<PathBuf>,
) -> Result<(), TranslateError> {
    let source = library_source(elf, appdir, lib_dirs)?;
    let stem = elf
        .file_stem()
        .and_then(|s| s.to_str())
        .unwrap_or(DEFAULT_PROGRAM_NAME);
    let exports = ShimExports::from_dll(&read_file(shim)?)?;
    let translation = translate::translate(
        ModuleName::sanitized(stem)?,
        read_file(elf)?,
        source.as_ref(),
        &exports,
    )?;
    let executable = bundle::write_bundle(&translation.files, shim, out)?;
    println!(
        "wrote {} ({} modules)",
        executable.display(),
        translation.files.len()
    );
    for report in &translation.reports {
        let stats = report.rewrite;
        if stats.canary_sites + stats.stub_sites > 0 {
            println!(
                "  {}: rewrote {} canary and {} thread-local access sites",
                report.name, stats.canary_sites, stats.stub_sites
            );
        }
    }
    Ok(())
}

fn show_analysis(elf: &Path) -> Result<(), TranslateError> {
    let info = analyze(&read_file(elf)?)?;
    println!("entry:      {:#x}", info.entry);
    println!("soname:     {}", info.soname.as_deref().unwrap_or("-"));
    println!("needed:     {}", info.needed.join(", "));
    for segment in &info.segments {
        println!(
            "segment:    vaddr {:#x} file {:#x} mem {:#x} {:?}",
            segment.vaddr, segment.file_size, segment.mem_size, segment.flags
        );
    }
    for symbol in info.symbols.iter().filter(|s| !s.name.is_empty()) {
        println!(
            "symbol:     {} {:?} {:?}",
            symbol.name, symbol.binding, symbol.definition
        );
    }
    println!("relocs:     {}", info.relocations.len());
    Ok(())
}
