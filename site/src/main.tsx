/*
 * File: site/src/main.tsx
 * Purpose: Entry point of the project page: applies the saved theme, then renders the app
 * Author: Akilesh
 * Date: 2026-11-04
 */

import "glisse";
import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { App } from "./App";
import { applyTheme, initialTheme } from "./lib/theme";
import "./styles/base.css";
import "./styles/sections.css";

const root = document.getElementById("root");
if (root === null) {
  throw new Error("index.html has no #root element");
}
applyTheme(initialTheme());
createRoot(root).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
