/*
 * File: site/src/three/pipeline-model.ts
 * Purpose: Where each block of the pipeline figure should be, and how it should look, at each of the six stages
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * The figure follows one small app through the stages: a program, two bundled libraries and two host libraries.
 * This module only says what the figure should look like; pipeline-scene.ts eases the blocks toward it.
 */

import type { Vec3 } from "./graph-layout";

export type BlockId = "program" | "libgreet" | "libmath" | "libc" | "libx11" | "hub";
export type BlockColor = "elf" | "pe" | "host" | "shim";

export interface BlockTarget {
  readonly position: Vec3;
  readonly color: BlockColor;
  readonly scale: number;
  readonly opacity: number;
}

export interface StageTargets {
  readonly blocks: Readonly<Record<BlockId, BlockTarget>>;
  /** Opacity of the translucent AppImage container (0 to 1). */
  readonly container: number;
  /** Opacity of the markers on rewritten instructions. */
  readonly markers: number;
  /** Opacity of the frame around the finished bundle. */
  readonly bundle: number;
  /** How visible the lines between blocks are. */
  readonly edges: number;
}

export const BLOCK_IDS: readonly BlockId[] = ["program", "libgreet", "libmath", "libc", "libx11", "hub"];
export const STAGE_COUNT = 6;

export const EDGES: readonly (readonly [BlockId, BlockId])[] = [
  ["program", "libgreet"],
  ["program", "libmath"],
  ["libgreet", "libmath"],
  ["program", "libc"],
  ["program", "libx11"],
  ["libgreet", "libc"],
  ["program", "hub"],
  ["libgreet", "hub"],
  ["libmath", "hub"],
];

const HUB: Vec3 = { x: 3, y: 0, z: 0 };
const MIN_SPREAD_FACTOR = 0.7;
const SPREAD_RANGE = 0.6;
const PACKED_SPACING = 0.9;

function at(x: number, y: number, z = 0): Vec3 {
  return { x, y, z };
}

function scaled(position: Vec3, factor: number): Vec3 {
  return { x: position.x * factor, y: position.y * factor, z: position.z * factor };
}

function block(position: Vec3, color: BlockColor, opacity = 1, scale = 1): BlockTarget {
  return { position, color, scale, opacity };
}

/** Stage 0: the AppDir sits packed inside the AppImage. The host libraries are not part of it. */
function packed(factor: number): Readonly<Record<BlockId, BlockTarget>> {
  const stacked = (row: number): Vec3 => scaled(at(0, (1 - row) * PACKED_SPACING), factor);
  return {
    program: block(stacked(0), "elf"),
    libgreet: block(stacked(1), "elf"),
    libmath: block(stacked(2), "elf"),
    libc: block(at(3, 1), "host", 0),
    libx11: block(at(3, -1), "host", 0),
    hub: block(HUB, "shim", 0, 0.4),
  };
}

function graph(factor: number, color: BlockColor): Readonly<Record<BlockId, BlockTarget>> {
  return {
    program: block(scaled(at(-3, 0), factor), color),
    libgreet: block(scaled(at(-0.6, 1.3), factor), color),
    libmath: block(scaled(at(-0.6, -1.3), factor), color),
    libc: block(scaled(at(3, 1.3), factor), "host", 0.7),
    libx11: block(scaled(at(3, -1.3), factor), "host", 0.7),
    hub: block(scaled(HUB, factor), "shim", 0, 0.4),
  };
}

/** Stage 4 and 5: the host libraries have been replaced by the shim, so they fold into the hub. */
function bound(factor: number): Readonly<Record<BlockId, BlockTarget>> {
  const base = graph(factor, "pe");
  const hub = scaled(HUB, factor);
  return {
    ...base,
    libc: block(hub, "shim", 0, 0.4),
    libx11: block(hub, "shim", 0, 0.4),
    hub: block(hub, "shim", 1, 1.2),
  };
}

function targetsFor(stage: number, factor: number): StageTargets {
  const none = { container: 0, markers: 0, bundle: 0, edges: 1 };
  switch (stage) {
    case 0:
      return { ...none, edges: 0, container: 0.4, blocks: packed(factor) };
    case 1:
      return { ...none, blocks: graph(factor, "elf") };
    case 2:
      return { ...none, blocks: graph(factor, "pe") };
    case 3:
      return { ...none, markers: 1, blocks: graph(factor, "pe") };
    case 4:
      return { ...none, markers: 1, blocks: bound(factor) };
    default:
      return { ...none, markers: 0, bundle: 1, blocks: bound(factor) };
  }
}

/** The targets for a stage (0 to 5) and a spread (0 to 1, how far apart the blocks sit). */
export function stageTargets(stage: number, spread: number): StageTargets {
  const clamped = Math.min(Math.max(Math.round(stage), 0), STAGE_COUNT - 1);
  const factor = MIN_SPREAD_FACTOR + SPREAD_RANGE * Math.min(Math.max(spread, 0), 1);
  return targetsFor(clamped, factor);
}
