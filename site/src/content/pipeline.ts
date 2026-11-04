/*
 * File: site/src/content/pipeline.ts
 * Purpose: The six steps from an AppImage to a Windows bundle, as the README and the translator describe them
 * Author: Akilesh
 * Date: 2026-11-04
 */

export interface PipelineStage {
  readonly id: string;
  readonly name: string;
  readonly summary: string;
  /** The command that performs this step on its own, or null when it only runs as part of `convert`. */
  readonly command: string | null;
}

export const CONVERT_COMMAND = "crossport convert App.AppImage --shim crossport_libc.dll --out C:/Apps/App";

export const PIPELINE_STAGES: readonly PipelineStage[] = [
  {
    id: "unpack",
    name: "Unpack",
    summary: "Find the SquashFS payload inside the AppImage and extract the AppDir: the program, its libraries and its data.",
    command: "crossport unpack App.AppImage --out appdir",
  },
  {
    id: "analyze",
    name: "Analyze",
    summary:
      "Read every ELF file and build the dependency graph. Each library is either bundled, so it gets translated, or a host library that the shim replaces.",
    command: "crossport analyze appdir/usr/bin/app",
  },
  {
    id: "relink",
    name: "Relink",
    summary: "Every ELF object becomes a Windows PE image, an .exe or a .dll, carrying the same machine code.",
    command: null,
  },
  {
    id: "rewrite",
    name: "Rewrite",
    summary:
      "The few instructions that only make sense on Linux are rewritten: thread-local storage reads, stack-protector canaries and system call sites.",
    command: null,
  },
  {
    id: "bind",
    name: "Bind",
    summary:
      "Imports are resolved against the translated libraries and the shim, which implements the Linux system interface on top of Win32.",
    command: null,
  },
  {
    id: "bundle",
    name: "Bundle",
    summary:
      "The result is an ordinary Windows folder: an .exe, its DLLs, the shim and a small environment file. Nothing is emulated at run time.",
    command: CONVERT_COMMAND,
  },
];
