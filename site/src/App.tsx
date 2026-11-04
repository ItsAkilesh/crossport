/*
 * File: site/src/App.tsx
 * Purpose: The page: header, the hero, the explainer sections, the platforms, the install guide and the footer
 * Author: Akilesh
 * Date: 2026-11-04
 */

import { useCallback, useState } from "react";
import { DistributionsSection } from "./components/DistributionsSection";
import { Footer } from "./components/Footer";
import { Header } from "./components/Header";
import { Hero } from "./components/Hero";
import { InstallSection } from "./components/InstallSection";
import { PipelineSection } from "./components/PipelineSection";
import { ProgramsSection } from "./components/ProgramsSection";
import { StatusSection } from "./components/StatusSection";
import { applyTheme, initialTheme, saveTheme, type Theme } from "./lib/theme";

export function App() {
  const [theme, setTheme] = useState<Theme>(initialTheme);
  const changeTheme = useCallback((next: Theme) => {
    applyTheme(next);
    saveTheme(next);
    setTheme(next);
  }, []);
  return (
    <>
      <a className="skip-link" href="#how">
        Skip to the explainer
      </a>
      <Header theme={theme} onThemeChange={changeTheme} />
      <main id="top" className="wrap">
        <Hero />
        <PipelineSection />
        <ProgramsSection />
        <StatusSection />
        <DistributionsSection />
        <InstallSection />
      </main>
      <Footer />
    </>
  );
}
