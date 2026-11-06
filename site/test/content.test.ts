/*
 * File: site/test/content.test.ts
 * Purpose: Tests that the page's content is consistent: programs, milestones and the pipeline match the sources they mirror
 * Author: Akilesh
 * Date: 2026-11-06
 */

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { PIPELINE_STAGES } from "../src/content/pipeline";
import { MILESTONES, doneTaskCount, parseMilestones } from "../src/content/milestones";
import { PROGRAMS, countByKind } from "../src/content/programs";

const GRAPH_SOURCE = fileURLToPath(new URL("../../translator/src/graph.rs", import.meta.url));

/** The libraries translator/src/graph.rs says the shim provides. */
function shimProvidedLibraries(): ReadonlySet<string> {
  const source = readFileSync(GRAPH_SOURCE, "utf8");
  const start = source.indexOf("SHIM_PROVIDED_LIBRARIES");
  const block = source.slice(start, source.indexOf("];", start));
  return new Set([...block.matchAll(/"([^"]+\.so[^"]*)"/g)].map((match) => match[1] ?? ""));
}

describe("programs", () => {
  it("has exactly one program module, and every dependency names a known module", () => {
    for (const program of PROGRAMS) {
      const ids = new Set(program.modules.map((module) => module.id));
      expect(ids.size, program.id).toBe(program.modules.length);
      expect(countByKind(program).program, program.id).toBe(1);
      for (const [from, to] of program.needs) {
        expect(ids.has(from), `${program.id}: ${from}`).toBe(true);
        expect(ids.has(to), `${program.id}: ${to}`).toBe(true);
      }
    }
  });

  it("marks a library as provided by the shim exactly when translator/src/graph.rs says so", () => {
    const provided = shimProvidedLibraries();
    expect(provided.size).toBeGreaterThan(10);
    for (const program of PROGRAMS) {
      for (const module of program.modules) {
        if (module.kind === "shim") {
          expect(provided.has(module.id), `${program.id}: ${module.id} should be provided`).toBe(true);
        }
        if (module.kind === "stubbed") {
          expect(provided.has(module.id), `${program.id}: ${module.id} should not be provided`).toBe(false);
        }
      }
    }
  });

  it("describes SDL2 as the measured 19 libraries: 8 provided and 11 stubbed", () => {
    const sdl2 = PROGRAMS.find((program) => program.id === "sdl2_window");
    expect(sdl2).toBeDefined();
    if (sdl2 === undefined) {
      return;
    }
    expect(countByKind(sdl2)).toEqual({ program: 1, translated: 1, shim: 8, stubbed: 11 });
    expect(sdl2.needs.filter(([from]) => from === "libSDL2-2.0.so.0")).toHaveLength(19);
  });
});

describe("milestones", () => {
  it("covers every milestone from M0 to M10 in order, starting done and ending planned", () => {
    expect(MILESTONES.map((milestone) => milestone.id)).toEqual(Array.from({ length: 11 }, (_, index) => `M${index}`));
    expect(MILESTONES[0]?.status).toBe("done");
    expect(MILESTONES[10]?.status).toBe("planned");
  });

  it("counts finished tasks", () => {
    const milestone = MILESTONES[0];
    expect(milestone).toBeDefined();
    if (milestone !== undefined) {
      expect(doneTaskCount(milestone)).toBe(milestone.tasks.length);
    }
  });

  it("rejects a malformed progress file instead of rendering nonsense", () => {
    expect(() => parseMilestones({})).toThrow();
    expect(() => parseMilestones({ milestones: [{ id: "M0" }] })).toThrow();
    expect(() => parseMilestones({ milestones: [{ id: "M0", title: "x", status: "bogus", tasks: [] }] })).toThrow();
    expect(() => parseMilestones({ milestones: [{ id: "M0", title: "x", status: "done", tasks: [{ title: 1 }] }] })).toThrow();
  });
});

describe("pipeline", () => {
  it("has the six stages in the order the translator runs them, each with a summary", () => {
    expect(PIPELINE_STAGES.map((stage) => stage.id)).toEqual(["unpack", "analyze", "relink", "rewrite", "bind", "bundle"]);
    for (const stage of PIPELINE_STAGES) {
      expect(stage.summary.length).toBeGreaterThan(30);
    }
  });
});
