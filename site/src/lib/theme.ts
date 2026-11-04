/*
 * File: site/src/lib/theme.ts
 * Purpose: Choose, remember and apply the light or dark theme
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * The first choice follows the system setting. A later choice is remembered in localStorage when it is
 * available; private windows and blocked storage fall back to the system setting without an error.
 */

export type Theme = "light" | "dark";

const STORAGE_KEY = "crossport-theme";

function isTheme(value: unknown): value is Theme {
  return value === "light" || value === "dark";
}

function storedTheme(): Theme | null {
  try {
    const stored = window.localStorage.getItem(STORAGE_KEY);
    return isTheme(stored) ? stored : null;
  } catch {
    return null;
  }
}

export function initialTheme(): Theme {
  return storedTheme() ?? (window.matchMedia("(prefers-color-scheme: light)").matches ? "light" : "dark");
}

export function applyTheme(theme: Theme): void {
  document.documentElement.dataset["theme"] = theme;
}

/** Remembers a deliberate choice, so the next visit starts with it. Applying a theme alone never saves. */
export function saveTheme(theme: Theme): void {
  try {
    window.localStorage.setItem(STORAGE_KEY, theme);
  } catch {
    // Storage is unavailable; the theme still applies for this visit.
  }
}
