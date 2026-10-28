// File: translator/src/coverage.rs
// Purpose: Measure which libc symbols a set of programs needs, how often, and how many the shim already provides
// Author: Akilesh
// Date: 2026-10-28

use std::collections::BTreeMap;

use crate::elf_model::{Definition, RelocKind};
use crate::error::TranslateError;
use crate::graph::ModuleGraph;
use crate::shim_exports::ShimExports;

/// One symbol the corpus needs from the C library (or whatever the shim stands in for).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SymbolUse {
    pub name: String,
    /// In how many programs the symbol is referenced.
    pub programs: usize,
    pub is_provided: bool,
    /// Every reference is weak, so a missing symbol resolves to null instead of failing the translation.
    pub is_weak_only: bool,
}

/// How one program fared: it loaded (with this many modules) or why it did not.
#[derive(Debug)]
pub struct ProgramOutcome {
    pub name: String,
    pub result: Result<usize, String>,
}

#[derive(Debug)]
pub struct Coverage {
    pub programs: Vec<ProgramOutcome>,
    /// Most used first, then by name.
    pub symbols: Vec<SymbolUse>,
}

impl Coverage {
    pub fn loaded_programs(&self) -> usize {
        self.programs.iter().filter(|p| p.result.is_ok()).count()
    }

    /// Symbols a translation would actually fail on: needed, strong somewhere, and not provided.
    pub fn blocking_symbols(&self) -> impl Iterator<Item = &SymbolUse> {
        self.symbols
            .iter()
            .filter(|s| !s.is_provided && !s.is_weak_only)
    }

    /// Fraction of (program, symbol) references the shim satisfies, weighting common symbols more.
    /// `None` when nothing was referenced.
    pub fn weighted_coverage(&self) -> Option<f64> {
        let strong = || self.symbols.iter().filter(|s| !s.is_weak_only);
        let total: usize = strong().map(|s| s.programs).sum();
        let provided: usize = strong().filter(|s| s.is_provided).map(|s| s.programs).sum();
        (total > 0).then(|| provided as f64 / total as f64)
    }
}

/// Counts each external symbol once per program. A symbol is external when some module
/// references it undefined and no module in the program's own graph defines it.
pub fn measure(
    programs: Vec<(String, Result<ModuleGraph, TranslateError>)>,
    shim: &ShimExports,
) -> Coverage {
    let mut outcomes = Vec::with_capacity(programs.len());
    let mut usage: BTreeMap<String, (usize, bool)> = BTreeMap::new();
    for (name, loaded) in programs {
        match loaded {
            Ok(graph) => {
                for (symbol, is_strong) in external_symbols(&graph) {
                    let entry = usage.entry(symbol).or_insert((0, false));
                    entry.0 += 1;
                    entry.1 |= is_strong;
                }
                outcomes.push(ProgramOutcome {
                    name,
                    result: Ok(graph.modules.len()),
                });
            }
            Err(error) => outcomes.push(ProgramOutcome {
                name,
                result: Err(error.to_string()),
            }),
        }
    }
    let mut symbols: Vec<SymbolUse> = usage
        .into_iter()
        .map(|(name, (programs, has_strong))| SymbolUse {
            is_provided: shim.provides(&name),
            is_weak_only: !has_strong,
            name,
            programs,
        })
        .collect();
    symbols.sort_by(|a, b| {
        b.programs
            .cmp(&a.programs)
            .then_with(|| a.name.cmp(&b.name))
    });
    Coverage {
        programs: outcomes,
        symbols,
    }
}

/// Strong, unresolved symbols the shim does not provide, sorted by name. A translation fails on exactly these.
pub fn missing_symbols(graph: &ModuleGraph, shim: &ShimExports) -> Vec<String> {
    external_symbols(graph)
        .into_iter()
        .filter(|(name, is_strong)| *is_strong && !shim.provides(name))
        .map(|(name, _)| name)
        .collect()
}

/// `(name, referenced strongly by at least one module)` for every symbol the graph needs from outside.
fn external_symbols(graph: &ModuleGraph) -> BTreeMap<String, bool> {
    let mut needed: BTreeMap<String, bool> = BTreeMap::new();
    add_copy_sources(graph, &mut needed);
    for module in &graph.modules {
        for symbol in &module.info.symbols {
            let is_undefined = symbol.definition == Definition::Undefined && !symbol.is_tls;
            if !is_undefined || symbol.name.is_empty() {
                continue;
            }
            let is_defined_inside = graph
                .modules
                .iter()
                .any(|m| m.exports.contains_key(&symbol.name));
            if !is_defined_inside {
                *needed.entry(symbol.name.clone()).or_insert(false) |= !symbol.is_weak();
            }
        }
    }
    needed
}

/// A copy relocation makes the executable "define" the symbol, but its initial value must still come
/// from a library (typically `stdout`, `stderr`, `environ` from the C library), so it is needed from outside.
fn add_copy_sources(graph: &ModuleGraph, needed: &mut BTreeMap<String, bool>) {
    for module in &graph.modules {
        for relocation in &module.info.relocations {
            if let RelocKind::Copy { symbol } = relocation.kind {
                let name = &module.info.symbols[symbol].name;
                let has_library_source = graph
                    .modules
                    .iter()
                    .skip(1)
                    .any(|m| m.exports.contains_key(name));
                if !has_library_source {
                    needed.insert(name.clone(), true);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::graph::load;
    use crate::naming::ModuleName;
    use crate::source::DirectorySource;
    use std::path::PathBuf;

    fn fixtures() -> PathBuf {
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures")
    }

    /// Loads `directory/file` (relative to the fixtures) as a program called `name`.
    fn loaded(
        directory: &str,
        file: &str,
        name: &str,
    ) -> (String, Result<ModuleGraph, TranslateError>) {
        let dir = fixtures().join(directory);
        let bytes = std::fs::read(dir.join(file)).expect("fixture exists");
        let graph = load(
            ModuleName::parse(name).expect("valid"),
            bytes,
            &DirectorySource::new(vec![dir]),
        );
        (name.to_string(), graph)
    }

    fn program(case: &str, exe: &str) -> (String, Result<ModuleGraph, TranslateError>) {
        loaded(case, exe, exe)
    }

    fn shim(names: &[&str]) -> ShimExports {
        ShimExports::from_names(names.iter().map(|n| n.to_string()))
    }

    fn find<'a>(coverage: &'a Coverage, name: &str) -> &'a SymbolUse {
        coverage
            .symbols
            .iter()
            .find(|s| s.name == name)
            .expect("symbol is listed")
    }

    #[test]
    fn symbols_defined_inside_the_program_are_not_counted_as_needed() {
        let coverage = measure(vec![program("two_libs", "two_libs")], &shim(&[]));
        assert!(coverage
            .symbols
            .iter()
            .all(|s| !s.name.starts_with("math_") && !s.name.starts_with("greet_")));
        assert_eq!(
            find(&coverage, "puts").programs,
            1,
            "used by three modules, counted once per program"
        );
    }

    #[test]
    fn usage_counts_programs_and_orders_the_most_used_first() {
        let programs = vec![
            program("two_libs", "two_libs"),
            loaded("", "hello_dyn.elf", "hello_dyn"),
            program("tls_basic", "tls_basic"),
        ];
        let coverage = measure(programs, &shim(&["puts"]));
        assert_eq!(coverage.loaded_programs(), 3);
        assert_eq!(coverage.symbols[0].programs, 3);
        assert_eq!(find(&coverage, "puts").programs, 3);
        assert_eq!(find(&coverage, "__stack_chk_fail").programs, 1);
    }

    #[test]
    fn provided_and_blocking_symbols_are_distinguished() {
        let coverage = measure(
            vec![program("tls_basic", "tls_basic")],
            &shim(&["puts", "__libc_start_main"]),
        );
        assert!(find(&coverage, "puts").is_provided);
        let blocking: Vec<&str> = coverage
            .blocking_symbols()
            .map(|s| s.name.as_str())
            .collect();
        assert!(blocking.contains(&"__tls_get_addr") && blocking.contains(&"__stack_chk_fail"));
        assert!(!blocking.contains(&"puts"));
    }

    #[test]
    fn data_symbols_reached_through_copy_relocations_count_as_needed() {
        let provided = shim(&["puts", "__libc_start_main"]);
        let coverage = measure(vec![program("two_libs", "two_libs")], &provided);
        assert!(
            coverage.symbols.iter().all(|s| s.name != "math_counter"),
            "a library provides that copy source"
        );
        let graph = program("two_libs", "two_libs").1.expect("loads");
        assert!(missing_symbols(&graph, &provided).is_empty());
    }

    #[test]
    fn weak_only_symbols_never_block() {
        let coverage = measure(
            vec![program("two_libs", "two_libs")],
            &shim(&["puts", "__libc_start_main"]),
        );
        assert!(find(&coverage, "__gmon_start__").is_weak_only);
        assert_eq!(coverage.blocking_symbols().count(), 0);
    }

    #[test]
    fn a_program_that_fails_to_load_is_reported_with_its_reason() {
        let missing = (
            "needs-libs".to_string(),
            load(
                ModuleName::parse("needs-libs").expect("valid"),
                include_bytes!("../tests/fixtures/two_libs/two_libs").to_vec(),
                &DirectorySource::new(vec![]),
            ),
        );
        let coverage = measure(vec![missing], &shim(&[]));
        assert_eq!(coverage.loaded_programs(), 0);
        assert!(coverage.programs[0]
            .result
            .as_ref()
            .expect_err("failed")
            .contains("libgreet.so.1"));
    }

    #[test]
    fn weighted_coverage_reflects_how_often_provided_symbols_are_used() {
        let all = measure(
            vec![program("tls_basic", "tls_basic")],
            &shim(&[
                "puts",
                "__libc_start_main",
                "__tls_get_addr",
                "__stack_chk_fail",
            ]),
        );
        assert_eq!(all.weighted_coverage(), Some(1.0));
        let half = measure(
            vec![program("tls_basic", "tls_basic")],
            &shim(&["puts", "__libc_start_main"]),
        );
        let ratio = half.weighted_coverage().expect("has references");
        assert!(ratio > 0.0 && ratio < 1.0);
        assert_eq!(measure(Vec::new(), &shim(&[])).weighted_coverage(), None);
    }
}
