/*
 * File: site/src/components/ScreenshotRow.tsx
 * Purpose: A row of captioned screenshots of the installer, served from public/screens
 * Author: Akilesh
 * Date: 2026-11-06
 */

import type { Screenshot } from "../content/updates";

const SCREEN_WIDTH = 620;
const SCREEN_HEIGHT = 440;

interface ScreenshotRowProps {
  readonly shots: readonly Screenshot[];
}

export function ScreenshotRow({ shots }: ScreenshotRowProps) {
  const base = `${import.meta.env.BASE_URL}screens/`;
  return (
    <div className="shots">
      {shots.map((shot) => (
        <figure key={shot.file}>
          <img src={`${base}${shot.file}`} alt={shot.alt} width={SCREEN_WIDTH} height={SCREEN_HEIGHT} loading="lazy" />
          <figcaption>{shot.caption}</figcaption>
        </figure>
      ))}
    </div>
  );
}
