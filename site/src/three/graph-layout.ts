/*
 * File: site/src/three/graph-layout.ts
 * Purpose: Place a program's modules in 3D: the program at the left, translated libraries beside it, and the shim's libraries and the stubbed ones in shells around the shim hub
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * Pure functions, so the layout can be tested without a renderer.
 */

import type { Program, ProgramModule } from "../content/programs";

export interface Vec3 {
  readonly x: number;
  readonly y: number;
  readonly z: number;
}

export const PROGRAM_POSITION: Vec3 = { x: -4.4, y: 0, z: 0 };
export const HUB_POSITION: Vec3 = { x: 3.2, y: 0, z: 0 };

const TRANSLATED_COLUMN_X = -1.6;
const TRANSLATED_SPACING = 1.5;
const SHIM_SHELL_RADIUS = 1.5;
const STUBBED_SHELL_RADIUS = 3.3;
const GOLDEN_ANGLE = Math.PI * (3 - Math.sqrt(5));

/** The i-th of `count` points spread evenly over a sphere (a Fibonacci lattice). */
export function spherePoint(index: number, count: number, radius: number, centre: Vec3): Vec3 {
  const height = 1 - (2 * (index + 0.5)) / count;
  const ring = Math.sqrt(1 - height * height);
  const angle = index * GOLDEN_ANGLE;
  return {
    x: centre.x + radius * Math.cos(angle) * ring,
    y: centre.y + radius * height,
    z: centre.z + radius * Math.sin(angle) * ring,
  };
}

function ofKind(program: Program, kind: ProgramModule["kind"]): readonly ProgramModule[] {
  return program.modules.filter((module) => module.kind === kind);
}

function translatedPositions(modules: readonly ProgramModule[]): readonly [string, Vec3][] {
  return modules.map((module, index): [string, Vec3] => [
    module.id,
    { x: TRANSLATED_COLUMN_X, y: (index - (modules.length - 1) / 2) * TRANSLATED_SPACING, z: 0 },
  ]);
}

function shellPositions(modules: readonly ProgramModule[], radius: number): readonly [string, Vec3][] {
  return modules.map((module, index): [string, Vec3] => [module.id, spherePoint(index, modules.length, radius, HUB_POSITION)]);
}

/** Every module's position, by id. */
export function layoutProgram(program: Program): ReadonlyMap<string, Vec3> {
  const positions = new Map<string, Vec3>();
  for (const module of ofKind(program, "program")) {
    positions.set(module.id, PROGRAM_POSITION);
  }
  const placed = [
    ...translatedPositions(ofKind(program, "translated")),
    ...shellPositions(ofKind(program, "shim"), SHIM_SHELL_RADIUS),
    ...shellPositions(ofKind(program, "stubbed"), STUBBED_SHELL_RADIUS),
  ];
  for (const [id, position] of placed) {
    positions.set(id, position);
  }
  return positions;
}

export function distance(first: Vec3, second: Vec3): number {
  return Math.hypot(first.x - second.x, first.y - second.y, first.z - second.z);
}
