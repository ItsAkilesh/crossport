/*
 * File: site/src/three/skyline-layout.ts
 * Purpose: Lay out the milestone skyline: one column per milestone, one cell per task, a base coloured by status
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * A pure function, so the layout can be tested without a renderer. Columns are centred on the visible
 * milestones, so hiding the planned ones recentres the skyline.
 */

import type { Milestone, MilestoneStatus } from "../content/milestones";

export interface SkylineCell {
  readonly milestoneId: string;
  readonly x: number;
  readonly y: number;
  readonly isDone: boolean;
}

export interface SkylineBase {
  readonly milestoneId: string;
  readonly x: number;
  readonly status: MilestoneStatus;
}

export interface Skyline {
  readonly cells: readonly SkylineCell[];
  readonly bases: readonly SkylineBase[];
  /** Half the width the columns span, for framing the camera. */
  readonly halfWidth: number;
  /** The height of the tallest column. */
  readonly height: number;
}

export const COLUMN_SPACING = 1.25;
export const CELL_SPACING = 0.36;
const COLUMN_MARGIN = 0.7;

export function layoutSkyline(milestones: readonly Milestone[], includePlanned: boolean): Skyline {
  const visible = milestones.filter((milestone) => includePlanned || milestone.status !== "planned");
  const cells: SkylineCell[] = [];
  const bases: SkylineBase[] = [];
  visible.forEach((milestone, column) => {
    const x = (column - (visible.length - 1) / 2) * COLUMN_SPACING;
    bases.push({ milestoneId: milestone.id, x, status: milestone.status });
    milestone.tasks.forEach((task, row) => {
      cells.push({ milestoneId: milestone.id, x, y: row * CELL_SPACING, isDone: task.isDone });
    });
  });
  const tallest = Math.max(0, ...visible.map((milestone) => milestone.tasks.length));
  return {
    cells,
    bases,
    halfWidth: (Math.max(visible.length - 1, 0) * COLUMN_SPACING) / 2 + COLUMN_MARGIN,
    height: tallest * CELL_SPACING,
  };
}
