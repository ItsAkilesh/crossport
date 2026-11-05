/*
 * File: site/src/components/PipelineSection.tsx
 * Purpose: The "How it works" section: pick a step of the translation and watch one small app go through it
 * Author: Akilesh
 * Date: 2026-11-05
 */

import { SegmentedControl, Slider } from "glisse";
import { useMemo, useState } from "react";
import { PIPELINE_STAGES } from "../content/pipeline";
import { loadPipelineScene } from "../three/loaders";
import type { PipelineInput } from "../three/pipeline-scene";
import { CommandBlock } from "./CommandBlock";
import { SceneCanvas } from "./SceneCanvas";
import { ScrollRow } from "./ScrollRow";
import { Section } from "./Section";

const STAGE_ITEMS = PIPELINE_STAGES.map((stage, index) => ({ value: String(index), label: stage.name }));
const DEFAULT_SPREAD = 0.5;
const SPREAD_STEP = 0.05;

export function PipelineSection() {
  const [stage, setStage] = useState(0);
  const [spread, setSpread] = useState(DEFAULT_SPREAD);
  const input = useMemo<PipelineInput>(() => ({ stage, spread }), [stage, spread]);
  const current = PIPELINE_STAGES[stage] ?? PIPELINE_STAGES[0];

  return (
    <Section
      id="how"
      eyebrow="How it works"
      title="Six steps from an AppImage to a Windows folder."
      intro="Both sides run the same architecture, so the machine code is kept. What changes is the container it sits in and the operating system it talks to. Step through it."
    >
      <div className="figure">
        <SceneCanvas
          load={loadPipelineScene}
          input={input}
          label={`A small app at the ${current?.name ?? ""} step: blocks for a program and its libraries, with lines showing what depends on what.`}
        />
        <div className="controls">
          <ScrollRow selection={String(stage)}>
            <SegmentedControl
              aria-label="Translation step"
              items={STAGE_ITEMS}
              value={String(stage)}
              onValueChange={(value) => setStage(Number(value))}
            />
          </ScrollRow>
          <label className="slider-row">
            <span>Spread</span>
            <Slider aria-label="Spread the blocks apart" value={spread} min={0} max={1} step={SPREAD_STEP} onValueChange={setSpread} />
          </label>
        </div>
      </div>
      {current === undefined ? null : (
        <article className="detail" aria-live="polite">
          <p className="eyebrow">
            Step {stage + 1} of {PIPELINE_STAGES.length}
          </p>
          <h3>{current.name}</h3>
          <p>{current.summary}</p>
          {current.command === null ? (
            <p className="fineprint">
              This step runs as part of <code>crossport convert</code>.
            </p>
          ) : (
            <CommandBlock command={current.command} label={`${current.name} command`} />
          )}
        </article>
      )}
    </Section>
  );
}
