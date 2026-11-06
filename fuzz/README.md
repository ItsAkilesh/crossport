<!-- File: fuzz/README.md -->
<!-- Purpose: How to run the translator fuzz targets locally and in CI, and how to turn a finding into a regression test -->
<!-- Author: Akilesh -->
<!-- Date: 2026-11-06 -->

# Fuzzing the translator

Targets (`src/lib.rs`): `analyze_elf`, `translate_exe`, `translate_lib`, `shim_pe`, `unpack_image`,
`unpack_squashfs`, `convert`, `cli_parse`. Seed corpora are in `corpus/<target>/`, built from the test fixtures
by `seedgen`. The threat model is in `docs/THREAT_REVIEW.md`.

## Coverage-guided (CI, Linux, nightly Rust)

```bash
cargo install cargo-fuzz --version =0.13.2 --locked
cd fuzz
cargo fuzz run --fuzz-dir . --features libfuzzer fuzz_translate_exe corpus/translate_exe -- -max_total_time=60 -timeout=20 -rss_limit_mb=2048
```

## Stable driver (any platform, no coverage guidance)

A mutation fuzzer with a hang watchdog (20 s) and a one GiB allocation cap (1 GiB); it builds with overflow checks.

```bash
cd fuzz
cargo run --release --bin driver -- translate_exe --seconds 60
```

A failing input is saved under `artifacts/<target>/`. Reproduce it by copying it to `corpus/<target>/`, or by
calling the target function from a test.

## Regenerating the seeds

```bash
cd fuzz
cargo run --release --features seedgen --bin seedgen -- corpus
```

## Turning a finding into a fix

Fix the smallest thing in `translator/src`, copy the input to `translator/tests/fixtures/fuzz/` and add a case to
`translator/tests/fuzz_regressions.rs` that fails without the fix.
