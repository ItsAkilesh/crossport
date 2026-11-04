/*
 * File: site/src/components/CopyButton.tsx
 * Purpose: A glass button that copies a command and says so
 * Author: Akilesh
 * Date: 2026-11-04
 */

import { Button } from "glisse";
import { useEffect, useState } from "react";
import { copyText } from "../lib/clipboard";

const CONFIRMATION_MS = 1800;

type CopyState = "idle" | "copied" | "failed";

export function CopyButton({ text, label }: { readonly text: string; readonly label: string }) {
  const [state, setState] = useState<CopyState>("idle");

  useEffect(() => {
    if (state === "idle") {
      return undefined;
    }
    const timer = window.setTimeout(() => setState("idle"), CONFIRMATION_MS);
    return () => window.clearTimeout(timer);
  }, [state]);

  const copy = async (): Promise<void> => setState((await copyText(text)) ? "copied" : "failed");
  const caption = state === "copied" ? "Copied" : state === "failed" ? "Copy failed" : "Copy";
  return (
    <Button variant="glass" aria-label={`${label}: ${caption}`} onClick={() => void copy()}>
      {caption}
    </Button>
  );
}
