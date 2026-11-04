/*
 * File: site/src/components/SceneCanvas.tsx
 * Purpose: A framed area that shows a three.js figure, with a text alternative for people who cannot see it
 * Author: Akilesh
 * Date: 2026-11-04
 */

import type { SceneLoader } from "../three/loaders";
import { useScene } from "../three/useScene";

interface SceneCanvasProps<Input> {
  readonly load: SceneLoader<Input>;
  readonly input: Input;
  /** What the figure shows, for assistive technology. */
  readonly label: string;
  readonly className?: string;
}

export function SceneCanvas<Input>({ load, input, label, className }: SceneCanvasProps<Input>) {
  const containerRef = useScene(load, input);
  return (
    <div ref={containerRef} className={className === undefined ? "scene" : `scene ${className}`} role="img" aria-label={label}>
      <span className="scene-fallback">The figure needs WebGL, which this browser is not providing.</span>
    </div>
  );
}
