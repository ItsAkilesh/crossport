// File: translator/src/graph.rs
// Purpose: Load the executable and every bundled library it needs into a module graph with a safe initialisation order
// Author: Akilesh
// Date: 2026-10-28

use std::collections::HashMap;

use crate::analyze::analyze;
use crate::elf_model::ElfInfo;
use crate::error::TranslateError;
use crate::naming::ModuleName;
use crate::source::{LibrarySource, MAX_ELF_BYTES};

/// Libraries the shim stands in for. They are never loaded from disk.
pub const SHIM_PROVIDED_LIBRARIES: &[&str] = &[
    "libc.so.6",
    // `__tls_get_addr` lives in the dynamic loader on Linux, so TLS users depend on it.
    "ld-linux-x86-64.so.2",
    // Folded into libc since glibc 2.34, still named by older toolchains.
    "libpthread.so.0",
    "libdl.so.2",
    "librt.so.1",
];
pub const MAX_MODULES: usize = 256;
/// Total bytes of ELF input across all modules.
const MAX_TOTAL_BYTES: u64 = 2 * MAX_ELF_BYTES;

pub struct Module {
    pub name: ModuleName,
    pub info: ElfInfo,
    pub data: Vec<u8>,
    /// Maps an exported symbol name to its index in `info.symbols`.
    pub exports: HashMap<String, usize>,
    /// Same for exported thread-local symbols, whose values are TLS offsets, not addresses.
    pub tls_exports: HashMap<String, usize>,
}

impl Module {
    fn new(name: ModuleName, data: Vec<u8>) -> Result<Self, TranslateError> {
        let info = analyze(&data)?;
        let mut exports = HashMap::new();
        let mut tls_exports = HashMap::new();
        for (index, symbol) in info.symbols.iter().enumerate() {
            if symbol.is_exported() {
                exports.entry(symbol.name.clone()).or_insert(index);
            } else if symbol.is_tls_exported() {
                tls_exports.entry(symbol.name.clone()).or_insert(index);
            }
        }
        Ok(Self {
            name,
            info,
            data,
            exports,
            tls_exports,
        })
    }
}

/// Index 0 is the executable. Libraries follow in breadth-first discovery
/// order, which is also the ELF global symbol lookup order.
pub struct ModuleGraph {
    pub modules: Vec<Module>,
    /// For each module, the modules named by its `DT_NEEDED`.
    pub dependencies: Vec<Vec<usize>>,
    /// Dependencies before dependents; the executable is last.
    pub init_order: Vec<usize>,
}

pub fn load(
    exe_name: ModuleName,
    exe_data: Vec<u8>,
    source: &dyn LibrarySource,
) -> Result<ModuleGraph, TranslateError> {
    let mut modules = vec![Module::new(exe_name, exe_data)?];
    let mut dependencies: Vec<Vec<usize>> = Vec::new();
    let mut index_by_name: HashMap<String, usize> = HashMap::new();
    let mut total_bytes = modules[0].data.len() as u64;
    let mut next = 0;
    while next < modules.len() {
        let needed = modules[next].info.needed.clone();
        let mut edges = Vec::new();
        for raw in needed
            .iter()
            .filter(|n| !SHIM_PROVIDED_LIBRARIES.contains(&n.as_str()))
        {
            let name = ModuleName::parse(raw)?;
            let index = match index_by_name.get(&name.case_folded()) {
                Some(&index) => index,
                None => add_library(
                    &mut modules,
                    &mut index_by_name,
                    &mut total_bytes,
                    name,
                    source,
                )?,
            };
            edges.push(index);
        }
        dependencies.push(edges);
        next += 1;
    }
    let init_order = initialization_order(&dependencies, &modules)?;
    Ok(ModuleGraph {
        modules,
        dependencies,
        init_order,
    })
}

fn add_library(
    modules: &mut Vec<Module>,
    index_by_name: &mut HashMap<String, usize>,
    total_bytes: &mut u64,
    name: ModuleName,
    source: &dyn LibrarySource,
) -> Result<usize, TranslateError> {
    if modules.len() >= MAX_MODULES {
        return Err(TranslateError::LimitExceeded(format!(
            "more than {MAX_MODULES} modules"
        )));
    }
    let data = source.load(&name)?;
    *total_bytes += data.len() as u64;
    if *total_bytes > MAX_TOTAL_BYTES {
        return Err(TranslateError::LimitExceeded(
            "total ELF input is too large".into(),
        ));
    }
    let index = modules.len();
    index_by_name.insert(name.case_folded(), index);
    modules.push(Module::new(name, data)?);
    Ok(index)
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Visit {
    InProgress,
    Done,
}

/// Depth-first post-order from the executable: every module appears after
/// everything it depends on. Cycles are reported with the offending path.
/// The walk is iterative so hostile graphs cannot overflow the call stack.
pub fn initialization_order(
    dependencies: &[Vec<usize>],
    modules: &[Module],
) -> Result<Vec<usize>, TranslateError> {
    let mut state: Vec<Option<Visit>> = vec![None; dependencies.len()];
    let mut order = Vec::with_capacity(dependencies.len());
    let mut stack: Vec<(usize, usize)> = vec![(0, 0)];
    state[0] = Some(Visit::InProgress);
    while let Some(&(module, edge)) = stack.last() {
        if let Some(&next) = dependencies[module].get(edge) {
            if let Some(top) = stack.last_mut() {
                top.1 += 1;
            }
            match state[next] {
                Some(Visit::InProgress) => return Err(cycle_error(&stack, next, modules)),
                Some(Visit::Done) => {}
                None => {
                    state[next] = Some(Visit::InProgress);
                    stack.push((next, 0));
                }
            }
        } else {
            state[module] = Some(Visit::Done);
            order.push(module);
            stack.pop();
        }
    }
    Ok(order)
}

fn cycle_error(stack: &[(usize, usize)], repeated: usize, modules: &[Module]) -> TranslateError {
    let start = stack.iter().position(|&(m, _)| m == repeated).unwrap_or(0);
    let mut path: Vec<&str> = stack[start..]
        .iter()
        .map(|&(m, _)| modules[m].name.as_str())
        .collect();
    path.push(modules[repeated].name.as_str());
    TranslateError::DependencyCycle(path.join(" -> "))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap as Map;

    struct MemorySource(Map<String, Vec<u8>>);

    impl LibrarySource for MemorySource {
        fn load(&self, name: &ModuleName) -> Result<Vec<u8>, TranslateError> {
            self.0
                .get(name.as_str())
                .cloned()
                .ok_or_else(|| TranslateError::MissingLibrary {
                    name: name.as_str().to_string(),
                })
        }
    }

    const EXE: &[u8] = include_bytes!("../tests/fixtures/two_libs/two_libs");
    const GREET: &[u8] = include_bytes!("../tests/fixtures/two_libs/libgreet.so.1");
    const MATH: &[u8] = include_bytes!("../tests/fixtures/two_libs/libmath.so.1");

    fn source() -> MemorySource {
        let mut files = Map::new();
        files.insert("libgreet.so.1".to_string(), GREET.to_vec());
        files.insert("libmath.so.1".to_string(), MATH.to_vec());
        MemorySource(files)
    }

    fn exe_name() -> ModuleName {
        ModuleName::parse("two_libs").expect("valid")
    }

    #[test]
    fn discovers_libraries_in_breadth_first_order_without_duplicates() {
        let graph = load(exe_name(), EXE.to_vec(), &source()).expect("loads");
        let names: Vec<&str> = graph.modules.iter().map(|m| m.name.as_str()).collect();
        assert_eq!(names, ["two_libs", "libgreet.so.1", "libmath.so.1"]);
        assert_eq!(graph.dependencies[0], [1, 2]);
        assert_eq!(graph.dependencies[1], [2]);
    }

    #[test]
    fn dependencies_initialise_before_dependents() {
        let graph = load(exe_name(), EXE.to_vec(), &source()).expect("loads");
        assert_eq!(
            graph.init_order,
            [2, 1, 0],
            "libmath, then libgreet, then the executable"
        );
    }

    #[test]
    fn missing_library_is_reported_by_name() {
        let empty = MemorySource(Map::new());
        let result = load(exe_name(), EXE.to_vec(), &empty);
        assert!(
            matches!(result, Err(TranslateError::MissingLibrary { name }) if name == "libgreet.so.1")
        );
    }

    #[test]
    fn shim_provided_libraries_are_not_loaded() {
        let graph = load(
            ModuleName::parse("hello").expect("valid"),
            include_bytes!("../tests/fixtures/hello_dyn.elf").to_vec(),
            &MemorySource(Map::new()),
        )
        .expect("loads");
        assert_eq!(graph.modules.len(), 1);
    }

    fn fake_modules(count: usize) -> Vec<Module> {
        (0..count)
            .map(|i| {
                let mut module = Module::new(
                    ModuleName::parse("libx.so.1").expect("valid"),
                    MATH.to_vec(),
                )
                .expect("parses");
                module.name = ModuleName::parse(&format!("m{i}")).expect("valid");
                module
            })
            .collect()
    }

    #[test]
    fn cycles_are_detected_with_their_path() {
        let modules = fake_modules(3);
        let result = initialization_order(&[vec![1], vec![2], vec![1]], &modules);
        assert!(
            matches!(result, Err(TranslateError::DependencyCycle(path)) if path == "m1 -> m2 -> m1")
        );
    }

    #[test]
    fn diamond_dependencies_are_ordered_once() {
        let modules = fake_modules(4);
        let order = initialization_order(&[vec![1, 2], vec![3], vec![3], vec![]], &modules)
            .expect("acyclic");
        assert_eq!(order, [3, 1, 2, 0]);
    }
}
