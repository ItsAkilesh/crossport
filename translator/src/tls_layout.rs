// File: translator/src/tls_layout.rs
// Purpose: Lay out every module's TLS block below the thread pointer, following the ELF x86-64 static TLS rules
// Author: Akilesh
// Date: 2026-10-28

use crate::elf_model::TlsSegment;
use crate::error::TranslateError;
use crate::graph::Module;
use crate::pe_model::align_up;

/// The thread pointer (and the thread control block that starts there) is aligned to at least this.
pub const MIN_THREAD_POINTER_ALIGN: u64 = 64;
/// Largest static TLS area for one thread.
const MAX_TOTAL_BYTES: u64 = 64 * 1024 * 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ModuleTls {
    pub segment: TlsSegment,
    /// Start of the module's block relative to the thread pointer (always negative).
    pub tp_offset: i64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TlsLayout {
    /// Indexed by module; `None` for modules without thread-local variables.
    pub modules: Vec<Option<ModuleTls>>,
    /// Bytes below the thread pointer, rounded to `max_align`.
    pub total_size: u64,
    pub max_align: u64,
}

impl TlsLayout {
    /// Variant II layout: the executable's block sits directly below the thread
    /// pointer, which is exactly what its local-exec offsets assume, and each
    /// library's block follows below it. Offsets of libraries are our choice
    /// because we resolve `TPOFF64` ourselves.
    pub fn compute(modules: &[Module]) -> Result<Self, TranslateError> {
        let mut layouts = Vec::with_capacity(modules.len());
        let mut offset = 0u64;
        let mut max_align = MIN_THREAD_POINTER_ALIGN;
        for module in modules {
            let Some(segment) = module.info.tls else {
                layouts.push(None);
                continue;
            };
            max_align = max_align.max(segment.align);
            offset = align_up(offset + segment.mem_size, segment.align);
            if offset > MAX_TOTAL_BYTES {
                return Err(TranslateError::LimitExceeded("static TLS is larger than 64 MiB".into()));
            }
            layouts.push(Some(ModuleTls { segment, tp_offset: -(offset as i64) }));
        }
        Ok(Self { modules: layouts, total_size: align_up(offset, max_align), max_align })
    }

    pub fn module(&self, index: usize) -> Option<&ModuleTls> {
        self.modules.get(index).and_then(Option::as_ref)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::graph::load;
    use crate::naming::ModuleName;
    use crate::source::DirectorySource;
    use std::path::PathBuf;

    fn graph(case: &str, exe: &str) -> crate::graph::ModuleGraph {
        let dir = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures").join(case);
        let bytes = std::fs::read(dir.join(exe)).expect("fixture exists");
        load(ModuleName::parse(exe).expect("valid"), bytes, &DirectorySource::new(vec![dir])).expect("loads")
    }

    #[test]
    fn the_executables_block_matches_its_local_exec_offsets() {
        let graph = graph("tls_basic", "tls_basic");
        let layout = TlsLayout::compute(&graph.modules).expect("lays out");
        let exe = layout.module(0).expect("executable has TLS");
        assert_eq!(exe.tp_offset, -0x70, "%fs:0xffffffffffffff90 in the real binary");
        assert!(layout.module(1).is_some(), "libtls has TLS");
    }

    #[test]
    fn library_blocks_sit_below_the_executable_without_overlap() {
        let graph = graph("tls_basic", "tls_basic");
        let layout = TlsLayout::compute(&graph.modules).expect("lays out");
        let exe = layout.module(0).expect("exe");
        let lib = layout.module(1).expect("lib");
        assert!(lib.tp_offset + lib.segment.mem_size as i64 <= exe.tp_offset);
        assert_eq!(lib.tp_offset % lib.segment.align as i64, 0);
        assert!(layout.total_size >= (-lib.tp_offset) as u64);
        assert_eq!(layout.total_size % layout.max_align, 0);
    }

    #[test]
    fn a_program_without_tls_still_gets_a_valid_empty_layout() {
        let graph = graph("two_libs", "two_libs");
        let layout = TlsLayout::compute(&graph.modules).expect("lays out");
        assert!(layout.modules.iter().all(Option::is_none));
        assert_eq!(layout.total_size, 0);
        assert_eq!(layout.max_align, MIN_THREAD_POINTER_ALIGN);
    }

    #[test]
    fn single_threaded_exe_offset_for_the_threads_case() {
        let graph = graph("threads", "threads");
        let layout = TlsLayout::compute(&graph.modules).expect("lays out");
        assert_eq!(layout.module(0).expect("exe").tp_offset, -8, "%fs:0xfffffffffffffff8");
    }
}
