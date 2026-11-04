/*
 * File: site/src/lib/clipboard.ts
 * Purpose: Copy text to the clipboard, reporting whether it worked
 * Author: Akilesh
 * Date: 2026-11-04
 */

/** Copies the text and resolves true; resolves false when the browser refuses or has no clipboard API. */
export async function copyText(text: string): Promise<boolean> {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    return false;
  }
}
