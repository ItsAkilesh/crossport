/*
 * File: site/src/components/ScrollRow.tsx
 * Purpose: A row that scrolls sideways inside its own box when its content is wider than the screen, and keeps the selected tab in view
 * Author: Akilesh
 * Date: 2026-11-05
 */

import { useEffect, useRef, type ReactNode } from "react";

interface ScrollRowProps {
  /** Changes whenever the selection does, so the selected tab is scrolled into view. */
  readonly selection: string;
  readonly children: ReactNode;
}

const SELECTED_TAB = '[aria-checked="true"]';

export function ScrollRow({ selection, children }: ScrollRowProps) {
  const row = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const selected = row.current?.querySelector<HTMLElement>(SELECTED_TAB);
    selected?.scrollIntoView({ inline: "nearest", block: "nearest" });
  }, [selection]);

  return (
    <div className="scroll-row" ref={row}>
      {children}
    </div>
  );
}
