/*
 * File: site/src/content/programs.ts
 * Purpose: The library dependencies of real corpus programs, and which Crossport translates, replaces or stubs
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * Measured with `crossport analyze` on the Linux ELF files that CI builds (the elf-corpus artifact), on
 * 2026-11-04. "shim" marks a library the shim provides (translator/src/graph.rs, SHIM_PROVIDED_LIBRARIES);
 * "stubbed" marks one that nobody provides, which `--stub-missing` binds to a function that returns 0. A test
 * checks these kinds against graph.rs so the page cannot drift from the translator.
 */

export type ModuleKind = "program" | "translated" | "shim" | "stubbed";

export interface ProgramModule {
  readonly id: string;
  readonly kind: ModuleKind;
}

export interface Program {
  readonly id: string;
  /** Short label for the selector. */
  readonly label: string;
  readonly title: string;
  readonly modules: readonly ProgramModule[];
  /** Pairs of module ids: the first needs the second. */
  readonly needs: readonly (readonly [string, string])[];
}

const SDL2_LIBRARY = "libSDL2-2.0.so.0";
const SDL2_SHIM_LIBRARIES = ["libc.so.6", "libm.so.6", "libX11.so.6"] as const;
const SDL2_STUBBED_LIBRARIES = [
  "libasound.so.2",
  "libpulse.so.0",
  "libsamplerate.so.0",
  "libXext.so.6",
  "libXcursor.so.1",
  "libXi.so.6",
  "libXfixes.so.3",
  "libXrandr.so.2",
  "libXss.so.1",
  "libdrm.so.2",
  "libgbm.so.1",
  "libwayland-egl.so.1",
  "libwayland-client.so.0",
  "libwayland-cursor.so.0",
  "libxkbcommon.so.0",
  "libdecor-0.so.0",
] as const;

function sdl2Program(): Program {
  const dependencies = [...SDL2_SHIM_LIBRARIES, ...SDL2_STUBBED_LIBRARIES];
  return {
    id: "sdl2_window",
    label: "SDL2",
    title: "An SDL2 program",
    modules: [
      { id: "sdl2_window", kind: "program" },
      { id: SDL2_LIBRARY, kind: "translated" },
      ...SDL2_SHIM_LIBRARIES.map((id): ProgramModule => ({ id, kind: "shim" })),
      ...SDL2_STUBBED_LIBRARIES.map((id): ProgramModule => ({ id, kind: "stubbed" })),
    ],
    needs: [["sdl2_window", SDL2_LIBRARY], ["sdl2_window", "libc.so.6"], ...dependencies.map((id): [string, string] => [SDL2_LIBRARY, id])],
  };
}

export const PROGRAMS: readonly Program[] = [
  {
    id: "two_libs",
    label: "Libraries",
    title: "A program with two bundled libraries",
    modules: [
      { id: "two_libs", kind: "program" },
      { id: "libgreet.so.1", kind: "translated" },
      { id: "libmath.so.1", kind: "translated" },
      { id: "libc.so.6", kind: "shim" },
    ],
    needs: [
      ["two_libs", "libgreet.so.1"],
      ["two_libs", "libmath.so.1"],
      ["two_libs", "libc.so.6"],
      ["libgreet.so.1", "libmath.so.1"],
      ["libgreet.so.1", "libc.so.6"],
      ["libmath.so.1", "libc.so.6"],
    ],
  },
  {
    id: "cxx_dynamic",
    label: "C++",
    title: "A C++ program with exceptions",
    modules: [
      { id: "cxx_dynamic", kind: "program" },
      { id: "libstdc++.so.6", kind: "translated" },
      { id: "libgcc_s.so.1", kind: "translated" },
      { id: "libc.so.6", kind: "shim" },
      { id: "libm.so.6", kind: "shim" },
      { id: "ld-linux-x86-64.so.2", kind: "shim" },
    ],
    needs: [
      ["cxx_dynamic", "libstdc++.so.6"],
      ["cxx_dynamic", "libgcc_s.so.1"],
      ["cxx_dynamic", "libc.so.6"],
      ["libstdc++.so.6", "libm.so.6"],
      ["libstdc++.so.6", "libc.so.6"],
      ["libstdc++.so.6", "ld-linux-x86-64.so.2"],
      ["libstdc++.so.6", "libgcc_s.so.1"],
      ["libgcc_s.so.1", "libc.so.6"],
      ["libgcc_s.so.1", "ld-linux-x86-64.so.2"],
    ],
  },
  {
    id: "gl_window",
    label: "OpenGL",
    title: "An OpenGL window",
    modules: [
      { id: "gl_window", kind: "program" },
      { id: "libGL.so.1", kind: "shim" },
      { id: "libX11.so.6", kind: "shim" },
      { id: "libc.so.6", kind: "shim" },
    ],
    needs: [
      ["gl_window", "libGL.so.1"],
      ["gl_window", "libX11.so.6"],
      ["gl_window", "libc.so.6"],
    ],
  },
  {
    id: "vulkan_window",
    label: "Vulkan",
    title: "A Vulkan window",
    modules: [
      { id: "vulkan_window", kind: "program" },
      { id: "libvulkan.so.1", kind: "shim" },
      { id: "libxcb.so.1", kind: "shim" },
      { id: "libc.so.6", kind: "shim" },
    ],
    needs: [
      ["vulkan_window", "libvulkan.so.1"],
      ["vulkan_window", "libxcb.so.1"],
      ["vulkan_window", "libc.so.6"],
    ],
  },
  sdl2Program(),
];

/** How many modules of each kind a program has. */
export function countByKind(program: Program): Readonly<Record<ModuleKind, number>> {
  const counts: Record<ModuleKind, number> = { program: 0, translated: 0, shim: 0, stubbed: 0 };
  for (const module of program.modules) {
    counts[module.kind] += 1;
  }
  return counts;
}
