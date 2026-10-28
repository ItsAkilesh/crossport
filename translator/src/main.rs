// File: translator/src/main.rs
// Purpose: crossport command-line entry point; exit code 0 = success, 2 = error
// Author: Akilesh
// Date: 2026-10-28

use std::fs;
use std::path::Path;
use std::process::ExitCode;

use crossport_translator::cli::{self, Action};
use crossport_translator::error::TranslateError;
use crossport_translator::{analyze, bundle, pe_writer, relink};

const EXIT_ERROR: u8 = 2;

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
        Action::Analyze { elf } => show_analysis(&elf),
        Action::Translate { elf, shim, out } => {
            let data = read_file(&elf)?;
            let image = relink::relink(&analyze::analyze(&data)?, &data)?;
            let name = elf.file_stem().and_then(|s| s.to_str()).unwrap_or("program");
            let exe = bundle::write_bundle(name, &pe_writer::write(&image)?, &shim, &out)?;
            println!("wrote {}", exe.display());
            Ok(())
        }
    }
}

fn read_file(path: &Path) -> Result<Vec<u8>, TranslateError> {
    fs::read(path).map_err(|source| TranslateError::Io {
        context: format!("reading {}", path.display()),
        source,
    })
}

fn show_analysis(elf: &Path) -> Result<(), TranslateError> {
    let info = analyze::analyze(&read_file(elf)?)?;
    println!("entry:      {:#x}", info.entry);
    println!("needed:     {}", info.needed.join(", "));
    for segment in &info.segments {
        println!(
            "segment:    vaddr {:#x} file {:#x} mem {:#x} {:?}",
            segment.vaddr, segment.file_size, segment.mem_size, segment.flags
        );
    }
    for import in &info.imports {
        let binding = if import.is_weak { "weak" } else { "strong" };
        println!("import:     {} ({binding})", import.name);
    }
    println!("relocs:     {}", info.relocations.len());
    Ok(())
}
