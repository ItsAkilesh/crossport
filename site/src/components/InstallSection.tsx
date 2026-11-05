/*
 * File: site/src/components/InstallSection.tsx
 * Purpose: How to build Crossport and convert an AppImage today (an installer can be built from source; there is no signed download yet), and what does not work yet
 * Author: Akilesh
 * Date: 2026-11-05
 */

import { INSTALL_STEPS, LIMITS, REPOSITORY_URL, REQUIREMENTS } from "../content/distributions";
import { CommandBlock } from "./CommandBlock";
import { Section } from "./Section";

export function InstallSection() {
  return (
    <Section
      id="install"
      eyebrow="Install"
      title="Build it once, then convert any AppImage."
      intro="There is no signed download yet. You build the translator and the shim from source, then give the translator an x86-64 Linux AppImage. The command line installs nothing system-wide; installer/package.sh builds a setup wizard and an AppImage launcher if you prefer double-clicking."
    >
      <div className="install-grid">
        <div>
          <h3>You need</h3>
          <ul className="plain">
            {REQUIREMENTS.map((requirement) => (
              <li key={requirement}>{requirement}</li>
            ))}
          </ul>
        </div>
        <ol className="steps">
          {INSTALL_STEPS.map((step) => (
            <li key={step.title}>
              <h3>{step.title}</h3>
              <CommandBlock command={step.command} label={step.title} />
            </li>
          ))}
        </ol>
      </div>
      <p>
        The converted folder is self-contained: run its <code>.exe</code> directly. The full guide, with troubleshooting
        and graphics driver notes, is in the{" "}
        <a href={`${REPOSITORY_URL}#use-crossport-on-windows`}>README</a>.
      </p>
      <div className="detail">
        <h3>Honest limits</h3>
        <ul className="plain">
          {LIMITS.map((limit) => (
            <li key={limit}>{limit}</li>
          ))}
        </ul>
      </div>
    </Section>
  );
}
