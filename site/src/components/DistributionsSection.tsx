/*
 * File: site/src/components/DistributionsSection.tsx
 * Purpose: The per-platform distributions: one project, a separate distribution for each target, each translating its own platform's AppImages
 * Author: Akilesh
 * Date: 2026-11-04
 */

import { Glass } from "glisse";
import { DISTRIBUTIONS } from "../content/distributions";
import { Section } from "./Section";

export function DistributionsSection() {
  return (
    <Section
      id="platforms"
      eyebrow="Platforms"
      title="One project. One distribution for each platform."
      intro="Crossport is not a one-size-fits-all package. Each target gets its own distribution, and each translates the AppImages built for its own architecture: no universal download, no fat bundle, no Rosetta."
    >
      <div className="cards">
        {DISTRIBUTIONS.map((distribution) => (
          <Glass key={distribution.id} className={`card platform-${distribution.id}`}>
            <p className="eyebrow">{distribution.isAvailable ? "Available to build" : "Planned"}</p>
            <h3>{distribution.platform}</h3>
            <p>Translates {distribution.translates}.</p>
            <p className="fineprint">{distribution.status}</p>
          </Glass>
        ))}
      </div>
      <p className="fineprint">An app that wants both publishes an x86-64 AppImage and an aarch64 AppImage, and each distribution converts the one that matches it.</p>
    </Section>
  );
}
