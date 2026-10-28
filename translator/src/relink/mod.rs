// File: translator/src/relink/mod.rs
// Purpose: Relink one ELF module of a graph into a PE image (executable or DLL) bound to its dependencies and the shim
// Author: Akilesh
// Date: 2026-10-28

mod layout;
mod patch;

use crate::base_reloc;
use crate::error::TranslateError;
use crate::exports::{self, ExportEntry};
use crate::graph::{Module, ModuleGraph};
use crate::imports::{self, ImportSet, ImportTables};
use crate::pe_model::{
    align_up, preferred_base, DataDirectory, ImageKind, PeImage, PeSection, PAGE_SIZE, RVA_BIAS,
    SCN_CODE, SCN_DISCARDABLE, SCN_EXECUTE, SCN_INITIALIZED_DATA, SCN_READ, SCN_WRITE,
};
use crate::resolve::Resolver;
use crate::rewrite::{self, RewriteStats};
use crate::shim_abi::{INFO_SYMBOL, SHIM_DLL, START_SYMBOL};
use crate::startup::{self, Binding, CopyRecord, ModuleInfo, TlsInfo};
use crate::tls_layout::TlsLayout;
use patch::{PatchContext, Patched};

const CANARY_CELL_BYTES: usize = 8;

/// A relinked module together with what the rewrite pass did to it.
pub struct Relinked {
    pub image: PeImage,
    pub stats: RewriteStats,
}

/// Hands out consecutive page-aligned RVAs for the sections added after the ELF segments.
struct SectionPlan {
    next_rva: u64,
    sections: Vec<PeSection>,
}

impl SectionPlan {
    fn next_rva(&self) -> u32 {
        self.next_rva as u32
    }

    fn push(&mut self, name: &str, data: Vec<u8>, characteristics: u32) {
        let rva = self.next_rva();
        self.next_rva += align_up(data.len() as u64, PAGE_SIZE).max(PAGE_SIZE);
        self.sections.push(PeSection {
            name: name.to_string(),
            rva,
            virtual_size: data.len() as u32,
            data,
            characteristics: SCN_INITIALIZED_DATA | characteristics,
        });
    }
}

pub fn relink_module(
    graph: &ModuleGraph,
    resolver: &Resolver<'_>,
    tls: &TlsLayout,
    index: usize,
) -> Result<Relinked, TranslateError> {
    let module = &graph.modules[index];
    let layout = layout::plan(&module.info.segments)?;
    let mut image = layout::build_image(&module.info, &module.data, &layout)?;
    let image_base = preferred_base(index);
    let context = PatchContext {
        graph,
        resolver,
        module: index,
        image_base,
        tls,
    };
    let patched = patch::apply(&mut image, &context)?;

    let mut plan = SectionPlan {
        next_rva: layout.end_rva,
        sections: Vec::new(),
    };
    let rewritten = rewrite_fs_accesses(&mut image, module, &mut plan)?;
    let mut sections = layout::elf_sections(&layout, &image);

    let is_executable = index == 0;
    let imports = import_tables(&patched, is_executable, plan.next_rva());
    plan.push(".idata", imports.data.clone(), SCN_READ | SCN_WRITE);

    let tls_info = tls_info(index, module, tls, &rewritten);
    let info = module_info(graph, index, module, &patched, &imports, tls_info)?;
    let startup_rva = plan.next_rva();
    let startup = if is_executable {
        let start_iat = imports
            .iat_rva(SHIM_DLL, START_SYMBOL)
            .ok_or_else(missing_import)?;
        startup::build_executable(startup_rva, &info, start_iat)?
    } else {
        startup::build_library(startup_rva, &info)?
    };
    let info_rva = startup_rva + startup.info_offset;
    plan.push(".cport", startup.data, SCN_READ | SCN_EXECUTE | SCN_CODE);

    let export_directory = if is_executable {
        DataDirectory::default()
    } else {
        push_exports(&mut plan, module, info_rva)
    };
    let base_reloc_directory = push_base_relocs(&mut plan, &patched);
    sections.append(&mut plan.sections);
    Ok(Relinked {
        image: PeImage {
            kind: if is_executable {
                ImageKind::Executable
            } else {
                ImageKind::Dll
            },
            image_base,
            entry_rva: if is_executable { startup_rva } else { 0 },
            sections,
            import_directory: imports.directory,
            export_directory,
            base_reloc_directory,
        },
        stats: rewritten.stats,
    })
}

/// What the FS-rewrite pass left behind for the rest of the pipeline.
struct Rewritten {
    stats: RewriteStats,
    canary_cell_rva: u32,
    /// RVAs of the 4-byte displacements the shim patches with the Windows TLS slot.
    slot_fixups: Vec<u32>,
}

/// Rewrites the module's FS-relative instructions and reserves the `.cdata`
/// (canary cell) and `.ctls` (stubs) sections they need.
fn rewrite_fs_accesses(
    image: &mut [u8],
    module: &Module,
    plan: &mut SectionPlan,
) -> Result<Rewritten, TranslateError> {
    let sites = rewrite::find_sites(image, &module.info)?;
    let mut canary_cell_rva = 0;
    if rewrite::count_canary_sites(&sites) > 0 {
        canary_cell_rva = plan.next_rva();
        plan.push(".cdata", vec![0; CANARY_CELL_BYTES], SCN_READ | SCN_WRITE);
    }
    let stubs_rva = plan.next_rva();
    let result = rewrite::apply(
        image,
        &sites,
        (canary_cell_rva != 0).then_some(canary_cell_rva),
        stubs_rva,
    )?;
    let slot_fixups = result
        .slot_fixups
        .iter()
        .map(|offset| stubs_rva + offset)
        .collect();
    if !result.stubs.is_empty() {
        plan.push(".ctls", result.stubs, SCN_READ | SCN_WRITE);
    }
    Ok(Rewritten {
        stats: result.stats,
        canary_cell_rva,
        slot_fixups,
    })
}

fn tls_info(index: usize, module: &Module, layout: &TlsLayout, rewritten: &Rewritten) -> TlsInfo {
    let mut info = TlsInfo {
        canary_cell_rva: rewritten.canary_cell_rva,
        slot_fixups: rewritten.slot_fixups.clone(),
        ..TlsInfo::default()
    };
    if let Some(block) = layout.module(index) {
        info.init_rva = (RVA_BIAS + block.segment.vaddr) as u32;
        info.file_size = block.segment.file_size as u32;
        info.mem_size = block.segment.mem_size as u32;
        info.align = block.segment.align as u32;
        info.offset = block.tp_offset as i32;
    }
    if index == 0 {
        info.total_size = layout.total_size as u32;
        info.max_align = layout.max_align as u32;
    }
    debug_assert_eq!(module.info.tls.is_some(), layout.module(index).is_some());
    info
}

fn missing_import() -> TranslateError {
    TranslateError::Elf("internal: an import slot is missing from the import tables".into())
}

fn import_tables(patched: &Patched, is_executable: bool, section_rva: u32) -> ImportTables {
    let mut set = ImportSet::default();
    for binding in &patched.import_bindings {
        set.add(&binding.dll, &binding.symbol);
    }
    for copy in &patched.copies {
        set.add(&copy.dll, &copy.symbol);
    }
    if is_executable {
        set.add(SHIM_DLL, START_SYMBOL);
    }
    imports::build(&set, section_rva)
}

fn module_info(
    graph: &ModuleGraph,
    index: usize,
    module: &Module,
    patched: &Patched,
    imports: &ImportTables,
    tls: TlsInfo,
) -> Result<ModuleInfo, TranslateError> {
    let rva = |vaddr: u64| (RVA_BIAS + vaddr) as u32;
    let elf = &module.info;
    let mut iat_bindings = Vec::with_capacity(patched.import_bindings.len());
    for binding in &patched.import_bindings {
        let iat_rva = imports
            .iat_rva(&binding.dll, &binding.symbol)
            .ok_or_else(missing_import)?;
        iat_bindings.push(Binding {
            slot_rva: binding.slot_rva,
            source_rva: iat_rva,
            addend: binding.addend,
        });
    }
    let mut copies = Vec::with_capacity(patched.copies.len());
    for copy in &patched.copies {
        let iat_rva = imports
            .iat_rva(&copy.dll, &copy.symbol)
            .ok_or_else(missing_import)?;
        copies.push(CopyRecord {
            dest_rva: copy.dest_rva,
            iat_rva,
            size: copy.size,
        });
    }
    Ok(ModuleInfo {
        entry_rva: if index == 0 { rva(elf.entry) } else { 0 },
        init_rva: elf.init.map_or(0, rva),
        init_array_rva: elf.init_array.map_or(0, |a| rva(a.vaddr)),
        init_array_count: elf.init_array.map_or(0, |a| a.count as u32),
        fini_rva: elf.fini.map_or(0, rva),
        fini_array_rva: elf.fini_array.map_or(0, |a| rva(a.vaddr)),
        fini_array_count: elf.fini_array.map_or(0, |a| a.count as u32),
        iat_bindings,
        exe_bindings: patched.executable_bindings.clone(),
        copies,
        dependencies: if index == 0 {
            dependency_dlls(graph)
        } else {
            Vec::new()
        },
        tls,
    })
}

/// DLL names in initialisation order, excluding the executable itself.
fn dependency_dlls(graph: &ModuleGraph) -> Vec<String> {
    graph
        .init_order
        .iter()
        .filter(|&&module| module != 0)
        .map(|&module| graph.modules[module].name.dll_file_name())
        .collect()
}

/// Exports every symbol the library defines plus the start-info pointer.
fn push_exports(plan: &mut SectionPlan, module: &Module, info_rva: u32) -> DataDirectory {
    let mut entries: Vec<ExportEntry> = module
        .exports
        .iter()
        .filter_map(|(name, &symbol)| {
            let address = module.info.symbols[symbol].address()?;
            Some(ExportEntry {
                name: name.clone(),
                rva: (RVA_BIAS + address) as u32,
            })
        })
        .collect();
    entries.push(ExportEntry {
        name: INFO_SYMBOL.to_string(),
        rva: info_rva,
    });
    let table = exports::build(&module.name.dll_file_name(), entries, plan.next_rva());
    plan.push(".edata", table.data, SCN_READ);
    table.directory
}

fn push_base_relocs(plan: &mut SectionPlan, patched: &Patched) -> DataDirectory {
    let data = base_reloc::build(&patched.base_reloc_slots);
    if data.is_empty() {
        return DataDirectory::default();
    }
    let directory = DataDirectory {
        rva: plan.next_rva(),
        size: data.len() as u32,
    };
    plan.push(".reloc", data, SCN_READ | SCN_DISCARDABLE);
    directory
}
