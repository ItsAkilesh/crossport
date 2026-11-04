/*
 * File: site/src/components/CommandBlock.tsx
 * Purpose: A shell command in a code block with a copy button
 * Author: Akilesh
 * Date: 2026-11-04
 */

import { CopyButton } from "./CopyButton";

export function CommandBlock({ command, label }: { readonly command: string; readonly label: string }) {
  return (
    <div className="command">
      <pre tabIndex={0} aria-label={label}>
        <code>{command}</code>
      </pre>
      <CopyButton text={command} label={label} />
    </div>
  );
}
