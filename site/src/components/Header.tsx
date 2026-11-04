/*
 * File: site/src/components/Header.tsx
 * Purpose: The sticky glass header: brand, section tabs (a menu on small screens), the theme switch and the source link
 * Author: Akilesh
 * Date: 2026-11-04
 */

import { Glass, Menu, Switch, TabBar } from "glisse";
import { REPOSITORY_URL } from "../content/distributions";
import type { Theme } from "../lib/theme";

const SECTIONS = [
  { value: "how", label: "How it works", href: "#how" },
  { value: "apps", label: "Your app", href: "#apps" },
  { value: "status", label: "Status", href: "#status" },
  { value: "install", label: "Install", href: "#install" },
] as const;

const NARROW_MENU_ITEMS = SECTIONS.map((section) => ({
  label: section.label,
  onSelect: () => {
    window.location.hash = section.href;
  },
}));

interface HeaderProps {
  readonly theme: Theme;
  readonly onThemeChange: (theme: Theme) => void;
}

export function Header({ theme, onThemeChange }: HeaderProps) {
  const logo = `${import.meta.env.BASE_URL}favicon.svg`;
  return (
    <header className="topbar-wrap">
      <Glass className="topbar">
        <a className="brand" href="#top">
          <img src={logo} alt="" width={28} height={28} />
          <span>Crossport</span>
        </a>
        <nav className="topbar-tabs" aria-label="Sections">
          <TabBar items={SECTIONS.map((section) => ({ ...section }))} defaultValue={null} />
        </nav>
        <div className="topbar-actions">
          <span className="topbar-menu">
            <Menu aria-label="Sections" align="end" items={NARROW_MENU_ITEMS} />
          </span>
          <label className="theme-switch">
            <span className="theme-switch-label">Dark</span>
            <Switch
              aria-label="Dark theme"
              checked={theme === "dark"}
              onCheckedChange={(isDark) => onThemeChange(isDark ? "dark" : "light")}
            />
          </label>
          <a className="pill" href={REPOSITORY_URL}>
            GitHub
          </a>
        </div>
      </Glass>
    </header>
  );
}
