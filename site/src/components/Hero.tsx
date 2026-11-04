/*
 * File: site/src/components/Hero.tsx
 * Purpose: The opening: the claim, what Crossport does in one paragraph, the two actions and the live hero figure
 * Author: Akilesh
 * Date: 2026-11-04
 */

import { REPOSITORY_URL } from "../content/distributions";
import { loadHeroScene } from "../three/loaders";
import { SceneCanvas } from "./SceneCanvas";

export function Hero() {
  return (
    <section className="hero" aria-labelledby="top-title">
      <div className="hero-copy">
        <p className="eyebrow">
          v{__CROSSPORT_VERSION__} &middot; early &middot; Windows x86-64 &middot; MIT
        </p>
        <h1 id="top-title">Linux apps, translated. Not emulated.</h1>
        <p className="lede">
          Crossport turns a Linux AppImage into a native Windows program. It translates the file format and the
          system interface, not the machine code, so the app runs at full speed with no virtual machine and no
          bundled browser.
        </p>
        <div className="actions">
          <a className="pill pill-primary" href="#install">
            Get started
          </a>
          <a className="pill" href={REPOSITORY_URL}>
            View source
          </a>
        </div>
        <p className="fineprint">
          Command-line tools, Vulkan, OpenGL and SDL2 programs work today. Qt and GTK are next.
        </p>
      </div>
      <SceneCanvas
        className="scene-hero"
        load={loadHeroScene}
        input={undefined}
        label="Blocks of code stream left to right through a glass gate, changing from amber for Linux to blue for Windows as they cross."
      />
    </section>
  );
}
