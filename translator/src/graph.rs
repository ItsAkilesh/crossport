// File: translator/src/graph.rs
// Purpose: Load the executable and every bundled library it needs into a module graph with a safe initialisation order
// Author: Akilesh
// Date: 2026-11-05

use std::collections::HashMap;
use std::path::PathBuf;

use crate::analyze::analyze;
use crate::elf_model::ElfInfo;
use crate::error::TranslateError;
use crate::naming::ModuleName;
use crate::source::{LibrarySource, SearchContext, MAX_ELF_BYTES};

/// Libraries the shim stands in for. They are never loaded from disk.
pub const SHIM_PROVIDED_LIBRARIES: &[&str] = &[
    "libc.so.6",
    // `__tls_get_addr` lives in the dynamic loader on Linux, so TLS users depend on it.
    "ld-linux-x86-64.so.2",
    // Folded into libc since glibc 2.34, still named by older toolchains.
    "libpthread.so.0",
    "libdl.so.2",
    "librt.so.1",
    // The math library is implemented by the shim as well.
    "libm.so.6",
    // The Vulkan loader is forwarded to the Windows loader (vulkan-1.dll) by the shim.
    "libvulkan.so.1",
    // The X11 wire library is provided by the shim on top of Win32 windows.
    "libxcb.so.1",
    // Xlib is provided in process on top of the same Win32 windows.
    "libX11.so.6",
    // XTest, which drives the in-process display's keyboard and pointer, is provided with it.
    "libXtst.so.6",
    // OpenGL and GLX are provided on top of opengl32.dll (WGL) and the same Win32 windows.
    "libGL.so.1",
    "libGLX.so.0",
    "libOpenGL.so.0",
    // EGL and the OpenGL ES libraries are provided on the same WGL layer.
    "libEGL.so.1",
    "libGLESv2.so.2",
    "libGLESv1_CM.so.1",
    // The resolver and utility libraries are folded into the shim as well.
    "libresolv.so.2",
    "libutil.so.1",
];
pub const MAX_MODULES: usize = 256;
/// Total bytes of ELF input across all modules.
const MAX_TOTAL_BYTES: u64 = 2 * MAX_ELF_BYTES;

pub struct Module {
    pub name: ModuleName,
    pub info: ElfInfo,
    pub data: Vec<u8>,
    /// Directory the module was read from, the `$ORIGIN` of its own dependencies.
    pub origin: Option<PathBuf>,
    /// Maps an exported symbol name to its index in `info.symbols`.
    pub exports: HashMap<String, usize>,
    /// Same for exported thread-local symbols, whose values are TLS offsets, not addresses.
    pub tls_exports: HashMap<String, usize>,
}

impl Module {
    fn new(
        name: ModuleName,
        data: Vec<u8>,
        origin: Option<PathBuf>,
    ) -> Result<Self, TranslateError> {
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
            origin,
            exports,
            tls_exports,
        })
    }
}

/// A library the program loads with `dlopen` instead of linking against: a toolkit plugin.
pub struct PluginFile {
    pub name: ModuleName,
    pub data: Vec<u8>,
    /// Directory the plugin was read from, the `$ORIGIN` of its own dependencies.
    pub origin: Option<PathBuf>,
}

/// Index 0 is the executable. Libraries follow in breadth-first discovery
/// order, which is also the ELF global symbol lookup order. Plugins and the
/// libraries only they need come last, from `lazy_start` on: they are loaded at
/// run time, so the startup and the global symbol scope never include them.
pub struct ModuleGraph {
    pub modules: Vec<Module>,
    /// For each module, the modules named by its `DT_NEEDED`.
    pub dependencies: Vec<Vec<usize>>,
    /// Dependencies before dependents; the executable is last. Startup modules only.
    pub init_order: Vec<usize>,
    /// Index of the first module loaded by `dlopen` (`modules.len()` when there is none).
    pub lazy_start: usize,
    /// For a lazy module: the lazy modules to initialise before it, dependencies first. Empty otherwise.
    pub lazy_init: Vec<Vec<usize>>,
    /// For each module, where its symbol references are looked up, in order: every startup module, then,
    /// for a lazy module, itself and the lazy modules it depends on.
    pub scopes: Vec<Vec<usize>>,
}

pub fn load(
    exe_name: ModuleName,
    exe_data: Vec<u8>,
    source: &dyn LibrarySource,
) -> Result<ModuleGraph, TranslateError> {
    load_collecting(exe_name, exe_data, source, Vec::new(), None)
}

/// Like `load`, but a library that cannot be found is recorded in `missing` and left out of the graph
/// instead of failing. For analysis of programs whose host libraries are not in the AppDir.
pub fn load_lenient(
    exe_name: ModuleName,
    exe_data: Vec<u8>,
    source: &dyn LibrarySource,
    missing: &mut Vec<String>,
) -> Result<ModuleGraph, TranslateError> {
    load_collecting(exe_name, exe_data, source, Vec::new(), Some(missing))
}

/// Loads the program with the plugins it opens at run time (see `ModuleGraph`). `missing` is as in `load_lenient`.
pub fn load_with_plugins(
    exe_name: ModuleName,
    exe_data: Vec<u8>,
    source: &dyn LibrarySource,
    plugins: Vec<PluginFile>,
    missing: Option<&mut Vec<String>>,
) -> Result<ModuleGraph, TranslateError> {
    load_collecting(exe_name, exe_data, source, plugins, missing)
}

struct Builder<'a> {
    modules: Vec<Module>,
    dependencies: Vec<Vec<usize>>,
    index_by_name: HashMap<String, usize>,
    total_bytes: u64,
    source: &'a dyn LibrarySource,
    missing: Option<&'a mut Vec<String>>,
}

impl Builder<'_> {
    /// Reads the `DT_NEEDED` of every module from `next` on, adding the libraries it names, until none is new.
    fn expand(&mut self, mut next: usize) -> Result<(), TranslateError> {
        while next < self.modules.len() {
            let needed = self.modules[next].info.needed.clone();
            let context = SearchContext {
                origin: self.modules[next].origin.clone(),
                rpath: self.modules[next].info.rpath.clone(),
                runpath: self.modules[next].info.runpath.clone(),
            };
            let mut edges = Vec::new();
            for raw in needed
                .iter()
                .filter(|n| !SHIM_PROVIDED_LIBRARIES.contains(&n.as_str()))
            {
                let name = ModuleName::parse(raw)?;
                if let Some(index) = self.find_or_add(name, &context)? {
                    edges.push(index);
                }
            }
            self.dependencies.push(edges);
            next += 1;
        }
        Ok(())
    }

    /// The index of a library already in the graph, or of the one just added; `None` when it is missing
    /// and missing libraries are being collected instead of failing.
    fn find_or_add(
        &mut self,
        name: ModuleName,
        context: &SearchContext,
    ) -> Result<Option<usize>, TranslateError> {
        if let Some(&index) = self.index_by_name.get(&name.case_folded()) {
            return Ok(Some(index));
        }
        let result = add_library(
            &mut self.modules,
            &mut self.index_by_name,
            &mut self.total_bytes,
            name,
            (self.source, context),
        );
        match result {
            Ok(index) => Ok(Some(index)),
            Err(TranslateError::MissingLibrary { name }) if self.missing.is_some() => {
                if let Some(list) = self.missing.as_deref_mut() {
                    if !list.contains(&name) {
                        list.push(name);
                    }
                }
                Ok(None)
            }
            Err(error) => Err(error),
        }
    }

    /// Adds a plugin; one that a startup module already is (its directory was scanned too) is skipped.
    fn add_plugin(&mut self, plugin: PluginFile, lazy_start: usize) -> Result<(), TranslateError> {
        match self.index_by_name.get(&plugin.name.case_folded()) {
            Some(&index) if index < lazy_start => return Ok(()),
            Some(_) => {
                return Err(TranslateError::Unsupported(format!(
                    "two plugins are named {}",
                    plugin.name.as_str()
                )))
            }
            None => {}
        }
        if self.modules.len() >= MAX_MODULES {
            return Err(TranslateError::LimitExceeded(format!(
                "more than {MAX_MODULES} modules"
            )));
        }
        self.total_bytes += plugin.data.len() as u64;
        if self.total_bytes > MAX_TOTAL_BYTES {
            return Err(TranslateError::LimitExceeded(
                "total ELF input is too large".into(),
            ));
        }
        self.index_by_name
            .insert(plugin.name.case_folded(), self.modules.len());
        self.modules
            .push(Module::new(plugin.name, plugin.data, plugin.origin)?);
        Ok(())
    }
}

fn load_collecting(
    exe_name: ModuleName,
    exe_data: Vec<u8>,
    source: &dyn LibrarySource,
    plugins: Vec<PluginFile>,
    missing: Option<&mut Vec<String>>,
) -> Result<ModuleGraph, TranslateError> {
    let executable = Module::new(exe_name, exe_data, source.executable_origin())?;
    let mut builder = Builder {
        total_bytes: executable.data.len() as u64,
        modules: vec![executable],
        dependencies: Vec::new(),
        index_by_name: HashMap::new(),
        source,
        missing,
    };
    builder.expand(0)?;
    let lazy_start = builder.modules.len();
    let init_order = initialization_order(
        &builder.dependencies[..lazy_start],
        &builder.modules[..lazy_start],
    )?;
    for plugin in plugins {
        builder.add_plugin(plugin, lazy_start)?;
    }
    builder.expand(lazy_start)?;
    let Builder {
        modules,
        dependencies,
        ..
    } = builder;
    let mut lazy_init = vec![Vec::new(); lazy_start];
    for module in lazy_start..modules.len() {
        let mut order = post_order(module, &dependencies, &modules, lazy_start)?;
        order.pop();
        lazy_init.push(order);
    }
    let scopes = build_scopes(&dependencies, lazy_start);
    Ok(ModuleGraph {
        modules,
        dependencies,
        init_order,
        lazy_start,
        lazy_init,
        scopes,
    })
}

/// The lookup scope of every module: all startup modules, then for a lazy module itself and what it needs.
fn build_scopes(dependencies: &[Vec<usize>], lazy_start: usize) -> Vec<Vec<usize>> {
    let global: Vec<usize> = (0..lazy_start).collect();
    (0..dependencies.len())
        .map(|module| {
            let mut scope = global.clone();
            if module >= lazy_start {
                let mut seen = vec![module];
                let mut next = 0;
                while next < seen.len() {
                    for &needed in &dependencies[seen[next]] {
                        if needed >= lazy_start && !seen.contains(&needed) {
                            seen.push(needed);
                        }
                    }
                    next += 1;
                }
                scope.extend(seen);
            }
            scope
        })
        .collect()
}

fn add_library(
    modules: &mut Vec<Module>,
    index_by_name: &mut HashMap<String, usize>,
    total_bytes: &mut u64,
    name: ModuleName,
    (source, context): (&dyn LibrarySource, &SearchContext),
) -> Result<usize, TranslateError> {
    if modules.len() >= MAX_MODULES {
        return Err(TranslateError::LimitExceeded(format!(
            "more than {MAX_MODULES} modules"
        )));
    }
    let loaded = source.load(&name, context)?;
    let data = loaded.bytes;
    *total_bytes += data.len() as u64;
    if *total_bytes > MAX_TOTAL_BYTES {
        return Err(TranslateError::LimitExceeded(
            "total ELF input is too large".into(),
        ));
    }
    let index = modules.len();
    index_by_name.insert(name.case_folded(), index);
    modules.push(Module::new(name, data, loaded.origin)?);
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
    post_order(0, dependencies, modules, 0)
}

/// Post-order from `root` over the modules at or above `floor`; lower modules are already loaded and left out.
fn post_order(
    root: usize,
    dependencies: &[Vec<usize>],
    modules: &[Module],
    floor: usize,
) -> Result<Vec<usize>, TranslateError> {
    let mut state: Vec<Option<Visit>> = vec![None; dependencies.len()];
    let mut order = Vec::with_capacity(dependencies.len());
    let mut stack: Vec<(usize, usize)> = vec![(root, 0)];
    state[root] = Some(Visit::InProgress);
    while let Some(&(module, edge)) = stack.last() {
        if let Some(&next) = dependencies[module].get(edge) {
            if let Some(top) = stack.last_mut() {
                top.1 += 1;
            }
            if next < floor {
                continue;
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
    use crate::source::LoadedLibrary;
    use std::collections::HashMap as Map;

    struct MemorySource(Map<String, Vec<u8>>);

    impl LibrarySource for MemorySource {
        fn load(
            &self,
            name: &ModuleName,
            _context: &SearchContext,
        ) -> Result<LoadedLibrary, TranslateError> {
            self.0
                .get(name.as_str())
                .map(|bytes| LoadedLibrary {
                    bytes: bytes.clone(),
                    origin: None,
                })
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

    fn plugin(name: &str, bytes: &[u8]) -> PluginFile {
        PluginFile {
            name: ModuleName::parse(name).expect("valid"),
            data: bytes.to_vec(),
            origin: None,
        }
    }

    fn graph_with_plugins(plugins: Vec<PluginFile>) -> ModuleGraph {
        load_with_plugins(
            ModuleName::parse("hello").expect("valid"),
            include_bytes!("../tests/fixtures/hello_dyn.elf").to_vec(),
            &source(),
            plugins,
            None,
        )
        .expect("loads")
    }

    #[test]
    fn plugins_come_after_the_startup_modules_and_bring_their_own_libraries() {
        let graph = graph_with_plugins(vec![plugin("libgreet.so.1", GREET)]);
        let names: Vec<&str> = graph.modules.iter().map(|m| m.name.as_str()).collect();
        assert_eq!(names, ["hello", "libgreet.so.1", "libmath.so.1"]);
        assert_eq!(graph.lazy_start, 1);
        assert_eq!(graph.init_order, [0], "startup does not include plugins");
        assert_eq!(
            graph.lazy_init[1],
            [2],
            "the plugin initialises its library first"
        );
        assert!(graph.lazy_init[0].is_empty() && graph.lazy_init[2].is_empty());
    }

    #[test]
    fn a_plugin_looks_symbols_up_in_the_startup_modules_then_itself_and_its_libraries() {
        let graph = graph_with_plugins(vec![plugin("libgreet.so.1", GREET)]);
        assert_eq!(graph.scopes[0], [0]);
        assert_eq!(graph.scopes[1], [0, 1, 2]);
        assert_eq!(
            graph.scopes[2],
            [0, 2],
            "a library of the plugin does not see the plugin"
        );
    }

    #[test]
    fn a_plugin_that_is_already_a_startup_library_is_not_added_again() {
        let exe = ModuleName::parse("two_libs").expect("valid");
        let graph = load_with_plugins(
            exe,
            EXE.to_vec(),
            &source(),
            vec![plugin("libgreet.so.1", GREET)],
            None,
        )
        .expect("loads");
        assert_eq!(graph.modules.len(), 3);
        assert_eq!(graph.lazy_start, 3);
    }

    #[test]
    fn two_plugins_with_one_name_are_refused() {
        let result = load_with_plugins(
            ModuleName::parse("hello").expect("valid"),
            include_bytes!("../tests/fixtures/hello_dyn.elf").to_vec(),
            &source(),
            vec![plugin("libx.so", MATH), plugin("libx.so", MATH)],
            None,
        );
        assert!(matches!(result, Err(TranslateError::Unsupported(_))));
    }

    fn fake_modules(count: usize) -> Vec<Module> {
        (0..count)
            .map(|i| {
                let mut module = Module::new(
                    ModuleName::parse("libx.so.1").expect("valid"),
                    MATH.to_vec(),
                    None,
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

    #[test]
    fn lenient_loading_records_missing_libraries_and_keeps_going() {
        let program = include_bytes!("../tests/fixtures/two_libs/two_libs").to_vec();
        let mut missing = Vec::new();
        let graph = load_lenient(
            ModuleName::parse("two_libs").expect("valid"),
            program.clone(),
            &MemorySource(Map::new()),
            &mut missing,
        )
        .expect("a missing library is not an error here");
        assert_eq!(graph.modules.len(), 1);
        assert!(!missing.is_empty());
        let strict = load(
            ModuleName::parse("two_libs").expect("valid"),
            program,
            &MemorySource(Map::new()),
        );
        assert!(matches!(strict, Err(TranslateError::MissingLibrary { .. })));
    }

    #[test]
    fn the_graphics_libraries_the_shim_implements_are_never_searched_for() {
        for name in [
            "libvulkan.so.1",
            "libxcb.so.1",
            "libX11.so.6",
            "libXtst.so.6",
            "libGL.so.1",
            "libGLX.so.0",
            "libOpenGL.so.0",
            "libEGL.so.1",
            "libGLESv2.so.2",
        ] {
            assert!(SHIM_PROVIDED_LIBRARIES.contains(&name), "{name}");
        }
    }
}
