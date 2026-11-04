/*
 * File: site/src/components/Section.tsx
 * Purpose: A page section with an eyebrow, a heading and an introduction, labelled for assistive technology
 * Author: Akilesh
 * Date: 2026-11-04
 */

import type { ReactNode } from "react";

interface SectionProps {
  readonly id: string;
  readonly eyebrow: string;
  readonly title: string;
  readonly intro?: ReactNode;
  readonly children: ReactNode;
}

export function Section({ id, eyebrow, title, intro, children }: SectionProps) {
  return (
    <section id={id} className="section" aria-labelledby={`${id}-title`}>
      <header className="section-head">
        <p className="eyebrow">{eyebrow}</p>
        <h2 id={`${id}-title`}>{title}</h2>
        {intro === undefined ? null : <p className="lede">{intro}</p>}
      </header>
      {children}
    </section>
  );
}
