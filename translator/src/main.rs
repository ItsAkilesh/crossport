// File: translator/src/main.rs
// Purpose: crossport command-line entry point; exit code 0 = success, 2 = error
// Author: Akilesh
// Date: 2026-10-28

use std::fs::File;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use crossport_translator::analyze::analyze;
use crossport_translator::appimage::{self, Limits};
use crossport_translator::cli::{self, Action};
use crossport_translator::error::TranslateError;
use crossport_translator::naming::ModuleName;
use crossport_translator::shim_exports::ShimExports;
use crossport_translator::source::{read_bounded, DirectorySource};
use crossport_translator::{bundle, translate};

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
        Action::Translate {
            elf,
            shim,
            out,
            lib_dirs,
        } => run_translation(&elf, &shim, &out, lib_dirs),
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

fn read_file(path: &Path) -> Result<Vec<u8>, TranslateError> {
    let file = File::open(path).map_err(|source| TranslateError::Io {
        context: format!("opening {}", path.display()),
        source,
    })?;
    read_bounded(file, &path.display().to_string())
}

fn run_translation(
    elf: &Path,
    shim: &Path,
    out: &Path,
    mut lib_dirs: Vec<PathBuf>,
) -> Result<(), TranslateError> {
    if lib_dirs.is_empty() {
        lib_dirs.push(
            elf.parent()
                .map_or_else(|| PathBuf::from("."), Path::to_path_buf),
        );
    }
    let stem = elf
        .file_stem()
        .and_then(|s| s.to_str())
        .unwrap_or(DEFAULT_PROGRAM_NAME);
    let exports = ShimExports::from_dll(&read_file(shim)?)?;
    let translation = translate::translate(
        ModuleName::sanitized(stem)?,
        read_file(elf)?,
        &DirectorySource::new(lib_dirs),
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
