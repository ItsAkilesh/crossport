// File: translator/src/translate.rs
// Purpose: Orchestrate a translation: load the module graph, resolve and relink every module, serialize the PE files
// Author: Akilesh
// Date: 2026-10-28

use crate::error::TranslateError;
use crate::graph;
use crate::naming::ModuleName;
use crate::pe_writer;
use crate::relink::relink_module;
use crate::resolve::Resolver;
use crate::shim_abi::START_SYMBOL;
use crate::shim_exports::ShimExports;
use crate::source::LibrarySource;

const EXECUTABLE_SUFFIX: &str = ".exe";

/// One file of the output bundle. `file_name` is always a validated plain name.
pub struct OutputFile {
    pub file_name: String,
    pub bytes: Vec<u8>,
}

/// Translates the executable and every library it needs. The executable is the first file returned.
pub fn translate(
    exe_name: ModuleName,
    exe_data: Vec<u8>,
    source: &dyn LibrarySource,
    shim: &ShimExports,
) -> Result<Vec<OutputFile>, TranslateError> {
    if !shim.provides(START_SYMBOL) {
        return Err(TranslateError::Unsupported(format!(
            "the shim does not export {START_SYMBOL}"
        )));
    }
    let graph = graph::load(exe_name, exe_data, source)?;
    let resolver = Resolver::new(&graph, shim);
    let mut files = Vec::with_capacity(graph.modules.len());
    for (index, module) in graph.modules.iter().enumerate() {
        let image = relink_module(&graph, &resolver, index)?;
        let file_name = if index == 0 {
            format!("{}{EXECUTABLE_SUFFIX}", module.name.as_str())
        } else {
            module.name.dll_file_name()
        };
        files.push(OutputFile {
            file_name,
            bytes: pe_writer::write(&image)?,
        });
    }
    Ok(files)
}
