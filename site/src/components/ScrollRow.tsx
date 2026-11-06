/*
 * File: site/src/components/ScrollRow.tsx
 * Purpose: A row that scrolls sideways inside its own box when its content is wider than the screen, and keeps the selected tab in view
 * Author: Akilesh
 * Date: 2026-11-06
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
    const box = row.current;
    const selected = box?.querySelector<HTMLElement>(SELECTED_TAB);
    if (!box || !selected) return;
    // Move only this row sideways. scrollIntoView also scrolls the page itself, which pulled the
    // whole page down to the tab row whenever it first rendered below the fold.
    const boxRect = box.getBoundingClientRect();
    const tabRect = selected.getBoundingClientRect();
    if (tabRect.left < boxRect.left) {
      box.scrollLeft -= boxRect.left - tabRect.left;
    } else if (tabRect.right > boxRect.right) {
      box.scrollLeft += tabRect.right - boxRect.right;
    }
  }, [selection]);

  return (
    <div className="scroll-row" ref={row}>
      {children}
    </div>
  );
}
