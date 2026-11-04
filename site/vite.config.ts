/*
 * File: site/vite.config.ts
 * Purpose: Build configuration for the Crossport project page, served from the /crossport/ path of GitHub Pages
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * The page's version label is read from the translator's manifest at build time, so it cannot drift from the
 * release it describes.
 */

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vitest/config";

const BASE_PATH = "/crossport/";
const MANIFEST_PATH = fileURLToPath(new URL("../translator/Cargo.toml", import.meta.url));
const VERSION_PATTERN = /^version\s*=\s*"([^"]+)"/m;

function translatorVersion(): string {
  const match = VERSION_PATTERN.exec(readFileSync(MANIFEST_PATH, "utf8"));
  if (match?.[1] === undefined) {
    throw new Error(`no version found in ${MANIFEST_PATH}`);
  }
  return match[1];
}

export default defineConfig({
  base: BASE_PATH,
  plugins: [react()],
  define: { __CROSSPORT_VERSION__: JSON.stringify(translatorVersion()) },
  build: { target: "es2022", sourcemap: false },
  test: { environment: "node", include: ["test/**/*.test.ts"] },
});
