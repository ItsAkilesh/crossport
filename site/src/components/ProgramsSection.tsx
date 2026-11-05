/*
 * File: site/src/components/ProgramsSection.tsx
 * Purpose: The "Your app" section: pick a real corpus program and see which of its libraries are translated, replaced by the shim or stubbed
 * Author: Akilesh
 * Date: 2026-11-05
 */

import { SegmentedControl } from "glisse";
import { useMemo, useState } from "react";
import { PROGRAMS, countByKind, type ModuleKind, type Program } from "../content/programs";
import type { GraphInput } from "../three/graph-scene";
import { loadGraphScene } from "../three/loaders";
import { SceneCanvas } from "./SceneCanvas";
import { ScrollRow } from "./ScrollRow";
import { Section } from "./Section";

const PROGRAM_ITEMS = PROGRAMS.map((program) => ({ value: program.id, label: program.label }));

const KIND_LABELS: Readonly<Record<ModuleKind, { readonly name: string; readonly meaning: string }>> = {
  program: { name: "The program", meaning: "translated into an .exe" },
  translated: { name: "Bundled libraries", meaning: "translated into DLLs" },
  shim: { name: "Replaced by the shim", meaning: "implemented on Win32 by Crossport" },
  stubbed: { name: "Stubbed", meaning: "nobody provides them, so calls return 0" },
};
const KIND_ORDER: readonly ModuleKind[] = ["program", "translated", "shim", "stubbed"];

interface LegendProps {
  readonly program: Program;
  readonly onHighlight: (id: string | null) => void;
}

function Legend({ program, onHighlight }: LegendProps) {
  return (
    <div className="legend">
      {KIND_ORDER.map((kind) => {
        const modules = program.modules.filter((module) => module.kind === kind);
        return modules.length === 0 ? null : (
          <div key={kind} className={`legend-group kind-${kind}`}>
            <h3>
              {KIND_LABELS[kind].name} <span>{modules.length}</span>
            </h3>
            <p className="fineprint">{KIND_LABELS[kind].meaning}</p>
            <ul>
              {modules.map((module) => (
                <li
                  key={module.id}
                  tabIndex={0}
                  onMouseEnter={() => onHighlight(module.id)}
                  onMouseLeave={() => onHighlight(null)}
                  onFocus={() => onHighlight(module.id)}
                  onBlur={() => onHighlight(null)}
                >
                  <code>{module.id}</code>
                </li>
              ))}
            </ul>
          </div>
        );
      })}
    </div>
  );
}

export function ProgramsSection() {
  const [programId, setProgramId] = useState(PROGRAMS[0]?.id ?? "");
  const [highlightedId, setHighlightedId] = useState<string | null>(null);
  const program = PROGRAMS.find((candidate) => candidate.id === programId) ?? PROGRAMS[0];
  const input = useMemo<GraphInput | null>(() => (program === undefined ? null : { program, highlightedId }), [program, highlightedId]);
  if (program === undefined || input === null) {
    return null;
  }
  const counts = countByKind(program);
  return (
    <Section
      id="apps"
      eyebrow="Your app"
      title="The system libraries become one shim."
      intro="An AppImage bundles some libraries and expects the rest from Linux. Crossport translates the ones it bundles, replaces the host ones with a shim that implements the Linux interface on Windows, and stubs what nobody provides. These are real programs from the test corpus, measured with the translator."
    >
      <div className="figure">
        <SceneCanvas
          load={loadGraphScene}
          input={input}
          label={`${program.title}: ${counts.translated} bundled libraries translated, ${counts.shim} host libraries replaced by the shim, ${counts.stubbed} stubbed.`}
        />
        <div className="controls">
          <ScrollRow selection={programId}>
            <SegmentedControl aria-label="Program" items={PROGRAM_ITEMS} value={programId} onValueChange={setProgramId} />
          </ScrollRow>
        </div>
      </div>
      <p className="summary" aria-live="polite">
        <strong>{program.title}.</strong> {counts.translated} translated, {counts.shim} replaced by the shim, {counts.stubbed}{" "}
        stubbed. Hover a name to find it in the figure.
      </p>
      <Legend program={program} onHighlight={setHighlightedId} />
    </Section>
  );
}
