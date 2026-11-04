/*
 * File: site/test/layout.test.ts
 * Purpose: Tests for the figures' layout logic: the dependency graph, the pipeline blocks and the milestone skyline
 * Author: Akilesh
 * Date: 2026-11-04
 */

import { describe, expect, it } from "vitest";
import type { Milestone } from "../src/content/milestones";
import { PROGRAMS } from "../src/content/programs";
import { HUB_POSITION, PROGRAM_POSITION, distance, layoutProgram, spherePoint, type Vec3 } from "../src/three/graph-layout";
import { BLOCK_IDS, EDGES, STAGE_COUNT, stageTargets } from "../src/three/pipeline-model";
import { CELL_SPACING, COLUMN_SPACING, layoutSkyline } from "../src/three/skyline-layout";

const MIN_NODE_GAP = 0.6;

function pointAt(points: readonly Vec3[], index: number): Vec3 {
  const point = points[index];
  if (point === undefined) {
    throw new Error(`no point at ${index}`);
  }
  return point;
}

describe("graph layout", () => {
  it("places every module of every program, finitely and without crowding", () => {
    for (const program of PROGRAMS) {
      const positions = layoutProgram(program);
      expect(positions.size, program.id).toBe(program.modules.length);
      const points = [...positions.values()];
      for (const point of points) {
        expect(Number.isFinite(point.x + point.y + point.z)).toBe(true);
      }
      for (let first = 0; first < points.length; first++) {
        for (let second = first + 1; second < points.length; second++) {
          const gap = distance(pointAt(points, first), pointAt(points, second));
          expect(gap, `${program.id}: ${first} and ${second}`).toBeGreaterThan(MIN_NODE_GAP);
        }
      }
    }
  });

  it("puts the program at the left and gives the same answer every time", () => {
    const sdl2 = PROGRAMS.find((program) => program.id === "sdl2_window");
    expect(sdl2).toBeDefined();
    if (sdl2 === undefined) {
      return;
    }
    expect(layoutProgram(sdl2).get("sdl2_window")).toEqual(PROGRAM_POSITION);
    expect([...layoutProgram(sdl2)]).toEqual([...layoutProgram(sdl2)]);
  });

  it("spreads points evenly over a sphere of the requested radius", () => {
    const radius = 2;
    for (let index = 0; index < 8; index++) {
      expect(distance(spherePoint(index, 8, radius, HUB_POSITION), HUB_POSITION)).toBeCloseTo(radius, 9);
    }
  });
});

describe("pipeline targets", () => {
  it("defines every block at every stage", () => {
    for (let stage = 0; stage < STAGE_COUNT; stage++) {
      const targets = stageTargets(stage, 0.5);
      for (const id of BLOCK_IDS) {
        expect(targets.blocks[id], `stage ${stage} ${id}`).toBeDefined();
      }
    }
    for (const [from, to] of EDGES) {
      expect(BLOCK_IDS).toContain(from);
      expect(BLOCK_IDS).toContain(to);
    }
  });

  it("keeps the host libraries out of the AppImage, then folds them into the shim hub", () => {
    expect(stageTargets(0, 0.5).blocks.libc.opacity).toBe(0);
    expect(stageTargets(1, 0.5).blocks.libc.opacity).toBeGreaterThan(0);
    const bound = stageTargets(4, 0.5);
    expect(bound.blocks.libc.opacity).toBe(0);
    expect(bound.blocks.libc.position).toEqual(bound.blocks.hub.position);
    expect(bound.blocks.hub.opacity).toBe(1);
  });

  it("recolours the program from ELF to PE at the relink step, and marks rewrites then", () => {
    expect(stageTargets(1, 0.5).blocks.program.color).toBe("elf");
    expect(stageTargets(2, 0.5).blocks.program.color).toBe("pe");
    expect(stageTargets(3, 0.5).markers).toBe(1);
    expect(stageTargets(2, 0.5).markers).toBe(0);
    expect(stageTargets(5, 0.5).bundle).toBe(1);
  });

  it("spreads the blocks further apart as the spread grows, and clamps out-of-range input", () => {
    const tight = stageTargets(1, 0).blocks.libgreet.position;
    const wide = stageTargets(1, 1).blocks.libgreet.position;
    expect(Math.abs(wide.y)).toBeGreaterThan(Math.abs(tight.y));
    expect(stageTargets(-5, 3)).toEqual(stageTargets(0, 1));
    expect(stageTargets(99, -1)).toEqual(stageTargets(STAGE_COUNT - 1, 0));
  });
});

describe("skyline layout", () => {
  const sample: readonly Milestone[] = [
    { id: "A", title: "a", status: "done", tasks: [{ title: "1", isDone: true }, { title: "2", isDone: true }] },
    { id: "B", title: "b", status: "next", tasks: [{ title: "1", isDone: true }, { title: "2", isDone: false }, { title: "3", isDone: false }] },
    { id: "C", title: "c", status: "planned", tasks: [{ title: "1", isDone: false }] },
  ];

  it("makes a cell per task and a base per milestone, centred on the columns", () => {
    const skyline = layoutSkyline(sample, true);
    expect(skyline.cells).toHaveLength(6);
    expect(skyline.bases.map((base) => base.milestoneId)).toEqual(["A", "B", "C"]);
    expect(skyline.bases.map((base) => base.x)).toEqual([-COLUMN_SPACING, 0, COLUMN_SPACING]);
    expect(skyline.height).toBeCloseTo(3 * CELL_SPACING, 9);
  });

  it("stacks a column's cells upwards in task order and records which are done", () => {
    const column = layoutSkyline(sample, true).cells.filter((cell) => cell.milestoneId === "B");
    expect(column.map((cell) => cell.y)).toEqual([0, CELL_SPACING, 2 * CELL_SPACING]);
    expect(column.map((cell) => cell.isDone)).toEqual([true, false, false]);
  });

  it("leaves out planned milestones and recentres when asked", () => {
    const skyline = layoutSkyline(sample, false);
    expect(skyline.bases.map((base) => base.milestoneId)).toEqual(["A", "B"]);
    expect(skyline.bases.map((base) => base.x)).toEqual([-COLUMN_SPACING / 2, COLUMN_SPACING / 2]);
    expect(skyline.cells.some((cell) => cell.milestoneId === "C")).toBe(false);
  });

  it("copes with no milestones at all", () => {
    expect(layoutSkyline([], true)).toEqual({ cells: [], bases: [], halfWidth: 0.7, height: 0 });
  });
});
