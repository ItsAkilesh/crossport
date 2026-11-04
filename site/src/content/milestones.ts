/*
 * File: site/src/content/milestones.ts
 * Purpose: The milestone list for the status section, read from docs/progress.json, the file that also drives the README graphic
 * Author: Akilesh
 * Date: 2026-11-04
 */

import progressData from "../../../docs/progress.json";

export type MilestoneStatus = "done" | "next" | "planned";

export interface MilestoneTask {
  readonly title: string;
  readonly isDone: boolean;
}

export interface Milestone {
  readonly id: string;
  readonly title: string;
  readonly status: MilestoneStatus;
  readonly tasks: readonly MilestoneTask[];
}

const STATUSES: readonly string[] = ["done", "next", "planned"];

function isRecord(value: unknown): value is Readonly<Record<string, unknown>> {
  return typeof value === "object" && value !== null;
}

function parseTask(value: unknown): MilestoneTask {
  if (!isRecord(value) || typeof value["title"] !== "string" || typeof value["done"] !== "boolean") {
    throw new Error("progress.json: a task needs a title and a done flag");
  }
  return { title: value["title"], isDone: value["done"] };
}

function parseMilestone(value: unknown): Milestone {
  if (!isRecord(value) || typeof value["id"] !== "string" || typeof value["title"] !== "string") {
    throw new Error("progress.json: a milestone needs an id and a title");
  }
  const status = value["status"];
  const tasks = value["tasks"];
  if (typeof status !== "string" || !STATUSES.includes(status) || !Array.isArray(tasks)) {
    throw new Error(`progress.json: milestone ${value["id"]} needs a known status and a task list`);
  }
  return { id: value["id"], title: value["title"], status: status as MilestoneStatus, tasks: tasks.map(parseTask) };
}

/** Validates the progress file's shape and returns its milestones in order. */
export function parseMilestones(value: unknown): readonly Milestone[] {
  if (!isRecord(value) || !Array.isArray(value["milestones"])) {
    throw new Error("progress.json: expected a milestones list");
  }
  return value["milestones"].map(parseMilestone);
}

export function doneTaskCount(milestone: Milestone): number {
  return milestone.tasks.filter((task) => task.isDone).length;
}

export const MILESTONES: readonly Milestone[] = parseMilestones(progressData);
