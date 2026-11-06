/*
 * File: site/src/components/UpdatesSection.tsx
 * Purpose: The "Latest" section: recent changes from the changelog
 * Author: Akilesh
 * Date: 2026-11-06
 */

import { UPDATES } from "../content/updates";
import { Section } from "./Section";

export function UpdatesSection() {
  return (
    <Section
      id="latest"
      eyebrow="Latest"
      title="What changed most recently."
      intro="Taken from the changelog: Milestone 7 reached a real Qt application with a window, and the Windows installer arrived."
    >
      <ol className="updates" aria-label="Recent changes">
        {UPDATES.map((update) => (
          <li key={update.version} className="detail">
            <p className="eyebrow">{update.version}</p>
            <h3>{update.title}</h3>
            <p>{update.summary}</p>
          </li>
        ))}
      </ol>
    </Section>
  );
}
