/*
 * File: site/src/components/StatusSection.tsx
 * Purpose: The "Where it stands" section: the milestone skyline, a switch for planned work, and the tasks of the chosen milestone
 * Author: Akilesh
 * Date: 2026-11-04
 */

import { Switch } from "glisse";
import { useMemo, useState } from "react";
import { MILESTONES, doneTaskCount } from "../content/milestones";
import { loadSkylineScene } from "../three/loaders";
import type { SkylineInput } from "../three/skyline-scene";
import { SceneCanvas } from "./SceneCanvas";
import { Section } from "./Section";

const FIRST_OPEN = MILESTONES.find((milestone) => milestone.status === "next")?.id ?? MILESTONES[0]?.id ?? null;
const CHECKED = "✓";
const UNCHECKED = "○";

export function StatusSection() {
  const [selectedId, setSelectedId] = useState<string | null>(FIRST_OPEN);
  const [includePlanned, setIncludePlanned] = useState(true);
  const input = useMemo<SkylineInput>(() => ({ milestones: MILESTONES, selectedId, includePlanned }), [selectedId, includePlanned]);
  const selected = MILESTONES.find((milestone) => milestone.id === selectedId);
  const finished = MILESTONES.filter((milestone) => milestone.status === "done").length;
  const tasks = MILESTONES.flatMap((milestone) => milestone.tasks);
  const doneTasks = tasks.filter((task) => task.isDone).length;

  return (
    <Section
      id="status"
      eyebrow="Where it stands"
      title="Real progress, read from the repository."
      intro={`${finished} of ${MILESTONES.length} milestones are done, and ${doneTasks} of ${tasks.length} tasks. Each cell is a task, lit when it is finished. The figure reads docs/progress.json, the file behind the README graphic.`}
    >
      <div className="figure">
        <SceneCanvas
          load={loadSkylineScene}
          input={input}
          label={`A skyline of ${MILESTONES.length} milestones, each a column of task cells, lit green for finished tasks.`}
        />
        <div className="controls">
          <label className="switch-row">
            <span>Show planned milestones</span>
            <Switch aria-label="Show planned milestones" checked={includePlanned} onCheckedChange={setIncludePlanned} />
          </label>
        </div>
      </div>
      <ol className="milestones" aria-label="Milestones">
        {MILESTONES.filter((milestone) => includePlanned || milestone.status !== "planned").map((milestone) => (
          <li key={milestone.id}>
            <button
              type="button"
              className={`milestone status-${milestone.status}`}
              aria-pressed={milestone.id === selectedId}
              onClick={() => setSelectedId(milestone.id === selectedId ? null : milestone.id)}
            >
              <span className="milestone-id">{milestone.id}</span>
              <span>{milestone.title}</span>
              <span className="milestone-count">
                {doneTaskCount(milestone)}/{milestone.tasks.length}
              </span>
            </button>
          </li>
        ))}
      </ol>
      {selected === undefined ? null : (
        <article className="detail" aria-live="polite">
          <p className="eyebrow">
            {selected.id} &middot; {selected.status}
          </p>
          <h3>{selected.title}</h3>
          <ul className="tasks">
            {selected.tasks.map((task) => (
              <li key={task.title} className={task.isDone ? "is-done" : undefined}>
                <span aria-hidden="true">{task.isDone ? CHECKED : UNCHECKED}</span>
                <span>
                  {task.title}
                  <span className="visually-hidden">{task.isDone ? " (done)" : " (not done)"}</span>
                </span>
              </li>
            ))}
          </ul>
        </article>
      )}
    </Section>
  );
}
