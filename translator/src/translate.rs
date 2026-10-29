// File: translator/src/translate.rs
// Purpose: Orchestrate a translation: load the module graph, resolve and relink every module, serialize the PE files
// Author: Akilesh
// Date: 2026-10-28

use crate::coverage::missing_symbols;
use crate::error::TranslateError;
use crate::graph;
use crate::naming::ModuleName;
use crate::pe_writer;
use crate::relink::relink_module;
use crate::resolve::Resolver;
use crate::rewrite::RewriteStats;
use crate::shim_abi::{START_SYMBOL, UNAVAILABLE_SYMBOL};
use crate::shim_exports::ShimExports;
use crate::source::LibrarySource;
use crate::tls_layout::TlsLayout;

const EXECUTABLE_SUFFIX: &str = ".exe";

/// One file of the output bundle. `file_name` is always a validated plain name.
pub struct OutputFile {
    pub file_name: String,
    pub bytes: Vec<u8>,
}

/// What the rewrite pass did to one module, for the CLI summary and coverage tracking.
pub struct ModuleReport {
    pub name: String,
    pub rewrite: RewriteStats,
}

pub struct Translation {
    /// The executable is first, then libraries in discovery order.
    pub files: Vec<OutputFile>,
    pub reports: Vec<ModuleReport>,
    /// With stubbing allowed: libraries that were not found and are left out.
    pub missing_libraries: Vec<String>,
    /// With stubbing allowed: symbols nobody provides, bound to the shim's unavailable function.
    pub stubbed_symbols: Vec<String>,
}

/// Whether symbols and libraries nobody provides stop the translation (the default) or are stubbed.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Missing {
    Fail,
    Stub,
}

/// Translates the executable and every library it needs.
pub fn translate(
    exe_name: ModuleName,
    exe_data: Vec<u8>,
    source: &dyn LibrarySource,
    shim: &ShimExports,
    on_missing: Missing,
) -> Result<Translation, TranslateError> {
    if !shim.provides(START_SYMBOL) {
        return Err(TranslateError::Unsupported(format!(
            "the shim does not export {START_SYMBOL}"
        )));
    }
    let mut missing_libraries = Vec::new();
    let graph = match on_missing {
        Missing::Fail => graph::load(exe_name, exe_data, source)?,
        Missing::Stub => graph::load_lenient(exe_name, exe_data, source, &mut missing_libraries)?,
    };
    let missing = missing_symbols(&graph, shim);
    if on_missing == Missing::Fail && !missing.is_empty() {
        return Err(TranslateError::UnresolvedSymbols(missing));
    }
    if on_missing == Missing::Stub && !shim.provides(UNAVAILABLE_SYMBOL) {
        return Err(TranslateError::Unsupported(format!(
            "the shim does not export {UNAVAILABLE_SYMBOL}"
        )));
    }
    let stubbed_symbols = missing.clone();
    let resolver = Resolver::new(&graph, shim).with_stubs(missing.into_iter().collect());
    let tls = TlsLayout::compute(&graph.modules)?;
    let mut files = Vec::with_capacity(graph.modules.len());
    let mut reports = Vec::with_capacity(graph.modules.len());
    for (index, module) in graph.modules.iter().enumerate() {
        let relinked = relink_module(&graph, &resolver, &tls, index)?;
        let file_name = if index == 0 {
            format!("{}{EXECUTABLE_SUFFIX}", module.name.as_str())
        } else {
            module.name.dll_file_name()
        };
        files.push(OutputFile {
            file_name,
            bytes: pe_writer::write(&relinked.image)?,
        });
        reports.push(ModuleReport {
            name: module.name.as_str().to_string(),
            rewrite: relinked.stats,
        });
    }
    Ok(Translation {
        files,
        reports,
        missing_libraries,
        stubbed_symbols,
    })
}
