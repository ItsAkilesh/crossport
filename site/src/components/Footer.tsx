/*
 * File: site/src/components/Footer.tsx
 * Purpose: The page footer: where to read more, the licence, and credit for the libraries the page uses
 * Author: Akilesh
 * Date: 2026-11-05
 */

import { AUTHOR_NAME, REPOSITORY_URL } from "../content/distributions";

const LINKS = [
  { label: "Source", href: REPOSITORY_URL },
  { label: "Design", href: `${REPOSITORY_URL}/blob/main/docs/DESIGN.md` },
  { label: "Roadmap", href: `${REPOSITORY_URL}/blob/main/docs/ROADMAP.md` },
  { label: "Changelog", href: `${REPOSITORY_URL}/blob/main/CHANGELOG.md` },
] as const;

export function Footer() {
  return (
    <footer className="footer">
      <nav aria-label="Project links">
        {LINKS.map((link) => (
          <a key={link.label} href={link.href}>
            {link.label}
          </a>
        ))}
      </nav>
      <p className="fineprint">
        Crossport v{__CROSSPORT_VERSION__} by {AUTHOR_NAME} &middot; MIT licence &middot; the figures use{" "}
        <a href="https://threejs.org">three.js</a> and the controls are <a href="https://github.com/0kzh/glisse">Glisse</a>.
      </p>
    </footer>
  );
}
