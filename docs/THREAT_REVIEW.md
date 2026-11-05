<!-- File: docs/THREAT_REVIEW.md -->
<!-- Purpose: Threat review of the translator's input handling (hostile AppImage, SquashFS and ELF input) and how it is fuzzed -->
<!-- Author: Akilesh -->
<!-- Date: 2026-11-05 -->

# Translator threat review (Milestone 9)

Scope: what the translator (`translator/src`) does with bytes an attacker controls. Not covered here: the shim
(paths, sockets, environment), which gets its own review.

## Trust model

`crossport convert` and `crossport unpack` take an AppImage from the internet and write files to disk. The
translator never runs anything from it. The output is a Windows program the user then chooses to run; what that
program does is outside this review. The translator itself must therefore treat the whole image as hostile:
it may crash with an error message, but it must not panic, loop for long, use unbounded memory or disk, or write
outside the output directory.

Trusted inputs: the command line, the shim DLL (built by this project), and the runtime directories the user names.

## Inputs and what stops each attack

| Input | Attack | Defence | Where |
|---|---|---|---|
| Entry path in the SquashFS | `..`, absolute paths, drive letters, `\` as a separator | Split on both separators, `..` is an error, names with `:` or other forbidden characters are errors | `appimage/paths.rs` `safe_components` |
| Entry path | Windows device names (`CON`, `NUL.txt`), trailing dot or space, control characters, 255 byte names, depth over 64 | Rejected, never rewritten | `appimage/paths.rs` `check_component` |
| Entry path | Two names that differ only by case, which collide on Windows | Rejected with both names | `Extraction::register` |
| Symlink | Target outside the archive, absolute target, loop, long chain | Resolved inside the archive only, at most 16 hops, otherwise skipped and reported; no symlink is ever created on disk, a file link becomes a copy | `appimage/extract.rs` `final_file`, `paths.rs` `resolve_link` |
| Symlink to a directory | Escape by a later write through the link | Directory links are skipped, so no write ever goes through a link | `Extraction::final_file` |
| Many links to one big file | Expansion beyond the size limit | Copied bytes count toward `max_total_bytes` | `Extraction::copy_link` |
| File size in the inode | Claims a huge size, or a size that differs from the data | Checked against `max_file_bytes` before writing; the copy is bounded to size plus one byte and a mismatch is an error | `Extraction::write_file` |
| Number of entries, total size | Zip-bomb style expansion | `max_entries`, `max_file_bytes`, `max_total_bytes` | `Limits` |
| Destination | Overwriting existing files | The output directory must be new or empty and files are opened with `create_new` | `prepare_destination`, `write_file` |
| Special files (devices, sockets) | Creating a device node | Never created, reported as skipped | `extract_entries` |
| File modes | setuid, setgid, sticky | Dropped on Unix hosts; Windows has no such bits | `apply_mode` |
| AppImage header | Section table offset or size that overflows or points outside the file | Checked add, bounds check, SquashFS magic check | `appimage/header.rs` |
| SquashFS metadata | Inconsistent inode, fragment and directory tables, overlapping blocks, huge block counts | Parsed by the `backhand` crate (pinned); every failure is an error, and the fuzz targets exercise this path | `unpack_squashfs`, `unpack_image` |
| ELF file | Truncated headers, segments or tables outside the file, overlapping `PT_LOAD` segments, `p_filesz` larger than `p_memsz`, huge `p_memsz` | Parsed with the `object` crate (pinned); the analysis checks bounds, and an image larger than the PE limits is an error | `analyze/*`, `elf_model.rs` |
| ELF file | Huge file | `read_bounded` stops at 256 MiB | `source.rs` |
| `DT_NEEDED` and `RUNPATH` | Names with `/`, `..` or odd characters that make the loader read a file outside the AppDir | `ModuleName::parse` accepts plain file names only; AppDir searches are confined to the AppDir, also through symlinks | `naming.rs`, `source.rs` `resolves_inside` |
| Library graph | Cycles, thousands of libraries | Cycle detection; at most 256 modules and 512 MiB of ELF data in one graph | `graph.rs` |
| Relocations, `.init_array`, TLS | Offsets outside a segment, counts that overflow | Checked arithmetic and bounds checks; static TLS capped at 64 MiB. The fuzzers found one unchecked addition (below) | `analyze/relocs.rs`, `tls_layout.rs`, `relink/patch.rs` |
| Output file names | A module name that is a path | Output names come from validated `ModuleName`s | `translate.rs` |

## Known gaps and decisions

- A SquashFS block that decompresses to far more than its stated size is bounded by `backhand` and by the
  `max_file_bytes` check, but the decompression time is not limited. The fuzz targets use a 20 second hang limit.
- `max_total_bytes` defaults to 4 GiB. A hostile image can fill that much disk before it is refused. This is a
  usability limit, not a safety limit, and `Limits` can be lowered by an embedder.
- The translator does not check the signature of an AppImage. Authenticity is the user's decision.
- Running a translated program is as dangerous as running the original on Linux. The sandbox and
  least-privilege defaults are the shim review.

## Fuzzing

`fuzz/` holds the harness (see `fuzz/README.md`): eight targets (ELF analysis, translating a hostile executable,
translating with a hostile library, the shim export reader, the whole AppImage unpack, the SquashFS reader behind a
valid header, `convert`, and the command line parser) with seed corpora built from the test fixtures. Each target
must return for any input: a panic, a run over 20 seconds, or a single allocation over 1 GiB (2 GiB resident under
libFuzzer) counts as a failure. `.github/workflows/fuzz.yml` runs a 60 second smoke run per target on every pull
request that touches `translator/` or `fuzz/`, and a long nightly run. The Milestone 9 exit criterion is 24 hours of
fuzzing without a translator crash, summed over the scheduled runs.

### Findings so far

| Found by | Input | Fix | Regression test |
|---|---|---|---|
| `translate_exe`, `translate_lib` | Addresses from the file (a symbol value, relocation offset, entry point, array or code range) near `u64::MAX` overflowed `RVA_BIAS + address` in `relink/patch.rs`, `relink/mod.rs` and `rewrite/scan.rs`: a panic in a debug build, a wrong address in a release build | One check in `analyze`: every such address must lie inside the 256 MiB module image limit, else `LimitExceeded` | `a_relocation_target_near_the_top_of_the_address_space_is_an_error`, `a_code_range_near_the_top_of_the_address_space_is_an_error`, `addresses_beyond_the_image_limit_are_an_error_wherever_they_come_from` |
| `unpack_image`, `unpack_squashfs`, `convert` | A SquashFS entry name that is not UTF-8 makes `backhand` 0.25.5 unwrap and panic on a Windows host (`v4/unix_string.rs`); on Linux the reader keeps the raw bytes | `unpack` catches the unwind and returns a malformed-archive error | `an_entry_name_that_is_not_utf8_does_not_panic_the_unpacker` |

The inputs are kept in `translator/tests/fixtures/fuzz/`. The driver found them within the first six minutes
of a build with overflow checks; the same driver without overflow checks found nothing in 13 minutes, which is why
the harness builds with them on.

The `backhand` panic should also be reported upstream; until a fixed release is pinned the catch in `unpack` stays.
