// File: translator/src/resolve.rs
// Purpose: Resolve symbol references once, at translation time, using ELF global scope lookup order
// Author: Akilesh
// Date: 2026-10-28

use crate::elf_model::{Definition, DynSymbol, SymbolBinding};
use crate::error::TranslateError;
use crate::graph::ModuleGraph;
use crate::shim_exports::ShimExports;

/// What a symbol reference binds to.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Target {
    /// Defined by the importing module itself, at this virtual address.
    Local { address: u64 },
    /// Defined by the executable (module 0), at this virtual address in the executable.
    Executable { address: u64 },
    /// Defined by another bundled library; bound through a PE import.
    Library { module: usize },
    /// Provided by the shim DLL; bound through a PE import.
    Shim,
    /// Weak and undefined everywhere: the slot becomes null.
    Null,
}

/// Where a thread-local symbol lives: its module and its offset inside that module's TLS block.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TlsTarget {
    pub module: usize,
    pub offset: u64,
}

pub struct Resolver<'a> {
    graph: &'a ModuleGraph,
    shim: &'a ShimExports,
}

impl<'a> Resolver<'a> {
    pub fn new(graph: &'a ModuleGraph, shim: &'a ShimExports) -> Self {
        Self { graph, shim }
    }

    /// ELF semantics: the first module in global scope order (executable, then
    /// libraries breadth-first) that exports the name wins, even when the
    /// importer defines the symbol itself (symbol interposition).
    pub fn resolve(&self, importer: usize, symbol: &DynSymbol) -> Result<Target, TranslateError> {
        match symbol.definition {
            Definition::Absolute => {
                return Err(TranslateError::Unsupported(format!(
                    "reference to absolute symbol {}",
                    symbol.name
                )))
            }
            Definition::Address(address) if symbol.binding == SymbolBinding::Local => {
                return Ok(Target::Local { address })
            }
            _ => {}
        }
        if let Some(definer) = self.first_definer(&symbol.name, 0) {
            return self.target_for(importer, definer, &symbol.name);
        }
        self.fallback(importer, symbol)
    }

    /// For `R_X86_64_COPY`: the library (or shim) whose data is copied into the executable.
    /// The executable's own definition is the destination, so it is skipped.
    pub fn resolve_copy_source(
        &self,
        importer: usize,
        symbol: &DynSymbol,
    ) -> Result<Target, TranslateError> {
        if let Some(definer) = self.first_definer(&symbol.name, 1) {
            return Ok(Target::Library { module: definer });
        }
        if self.shim.provides(&symbol.name) {
            return Ok(Target::Shim);
        }
        Err(self.unresolved(importer, symbol))
    }

    /// Resolves a TLS relocation. `None` means "the importing module itself".
    /// Thread-local symbols follow the same scope order as ordinary ones.
    pub fn resolve_tls(
        &self,
        importer: usize,
        symbol: Option<&DynSymbol>,
    ) -> Result<TlsTarget, TranslateError> {
        let Some(symbol) = symbol else {
            return Ok(TlsTarget {
                module: importer,
                offset: 0,
            });
        };
        if !symbol.is_tls {
            return Err(TranslateError::Elf(format!(
                "TLS relocation against non-TLS symbol {}",
                symbol.name
            )));
        }
        if let Some(address) = symbol
            .address()
            .filter(|_| symbol.binding == SymbolBinding::Local)
        {
            return Ok(TlsTarget {
                module: importer,
                offset: address,
            });
        }
        for (module, candidate) in self.graph.modules.iter().enumerate() {
            if let Some(&index) = candidate.tls_exports.get(&symbol.name) {
                let offset = candidate.info.symbols[index].address().unwrap_or(0);
                return Ok(TlsTarget { module, offset });
            }
        }
        Err(self.unresolved(importer, symbol))
    }

    fn first_definer(&self, name: &str, from: usize) -> Option<usize> {
        (from..self.graph.modules.len())
            .find(|&index| self.graph.modules[index].exports.contains_key(name))
    }

    fn target_for(
        &self,
        importer: usize,
        definer: usize,
        name: &str,
    ) -> Result<Target, TranslateError> {
        let module = &self.graph.modules[definer];
        let index = *module
            .exports
            .get(name)
            .ok_or_else(|| TranslateError::Elf("export index vanished".into()))?;
        let address = module.info.symbols[index]
            .address()
            .ok_or_else(|| TranslateError::Elf("exported symbol has no address".into()))?;
        Ok(match definer {
            _ if definer == importer => Target::Local { address },
            0 => Target::Executable { address },
            _ => Target::Library { module: definer },
        })
    }

    fn fallback(&self, importer: usize, symbol: &DynSymbol) -> Result<Target, TranslateError> {
        if self.shim.provides(&symbol.name) {
            Ok(Target::Shim)
        } else if symbol.is_weak() {
            Ok(Target::Null)
        } else {
            Err(self.unresolved(importer, symbol))
        }
    }

    fn unresolved(&self, importer: usize, symbol: &DynSymbol) -> TranslateError {
        TranslateError::UnresolvedSymbol {
            symbol: symbol.name.clone(),
            module: self.graph.modules[importer].name.as_str().to_string(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::graph::load;
    use crate::naming::ModuleName;
    use crate::source::LibrarySource;
    use crate::source::{LoadedLibrary, SearchContext};
    use std::collections::HashMap;

    struct Files(HashMap<String, Vec<u8>>);

    impl LibrarySource for Files {
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
                    name: name.as_str().into(),
                })
        }
    }

    fn graph() -> ModuleGraph {
        let mut files = HashMap::new();
        files.insert(
            "libgreet.so.1".to_string(),
            include_bytes!("../tests/fixtures/two_libs/libgreet.so.1").to_vec(),
        );
        files.insert(
            "libmath.so.1".to_string(),
            include_bytes!("../tests/fixtures/two_libs/libmath.so.1").to_vec(),
        );
        let exe = include_bytes!("../tests/fixtures/two_libs/two_libs").to_vec();
        load(
            ModuleName::parse("two_libs").expect("valid"),
            exe,
            &Files(files),
        )
        .expect("loads")
    }

    fn shim() -> ShimExports {
        ShimExports::from_names([
            "puts".to_string(),
            "__libc_start_main".to_string(),
            "crossport_start".to_string(),
        ])
    }

    fn symbol<'a>(graph: &'a ModuleGraph, module: usize, name: &str) -> &'a DynSymbol {
        graph.modules[module]
            .info
            .symbols
            .iter()
            .find(|s| s.name == name)
            .expect("symbol present")
    }

    #[test]
    fn executable_binds_library_functions_through_imports() {
        let graph = graph();
        let shim = shim();
        let resolver = Resolver::new(&graph, &shim);
        let target = resolver
            .resolve(0, symbol(&graph, 0, "greet_run"))
            .expect("resolves");
        assert_eq!(target, Target::Library { module: 1 });
    }

    #[test]
    fn library_references_to_copied_data_bind_to_the_executables_copy() {
        let graph = graph();
        let shim = shim();
        let resolver = Resolver::new(&graph, &shim);
        let target = resolver
            .resolve(2, symbol(&graph, 2, "math_counter"))
            .expect("resolves");
        assert!(matches!(target, Target::Executable { address } if address == 0x4010));
    }

    #[test]
    fn copy_source_skips_the_executables_own_definition() {
        let graph = graph();
        let shim = shim();
        let resolver = Resolver::new(&graph, &shim);
        let target = resolver
            .resolve_copy_source(0, symbol(&graph, 0, "math_counter"))
            .expect("resolves");
        assert_eq!(target, Target::Library { module: 2 });
    }

    #[test]
    fn libc_symbols_bind_to_the_shim_and_missing_weak_ones_to_null() {
        let graph = graph();
        let shim = shim();
        let resolver = Resolver::new(&graph, &shim);
        assert_eq!(
            resolver
                .resolve(1, symbol(&graph, 1, "puts"))
                .expect("resolves"),
            Target::Shim
        );
        assert_eq!(
            resolver
                .resolve(1, symbol(&graph, 1, "__gmon_start__"))
                .expect("resolves"),
            Target::Null
        );
    }

    #[test]
    fn a_strong_symbol_nobody_provides_is_an_error() {
        let graph = graph();
        let empty = ShimExports::from_names(Vec::new());
        let resolver = Resolver::new(&graph, &empty);
        let result = resolver.resolve(1, symbol(&graph, 1, "puts"));
        assert!(
            matches!(result, Err(TranslateError::UnresolvedSymbol { symbol, module }) if symbol == "puts" && module == "libgreet.so.1")
        );
    }

    #[test]
    fn a_module_binds_its_own_symbols_locally() {
        let graph = graph();
        let shim = shim();
        let resolver = Resolver::new(&graph, &shim);
        let target = resolver
            .resolve(2, symbol(&graph, 2, "math_add"))
            .expect("resolves");
        assert_eq!(target, Target::Local { address: 0x1130 });
    }
}
