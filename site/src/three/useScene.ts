/*
 * File: site/src/three/useScene.ts
 * Purpose: A React hook that loads a figure on demand, mounts it in a container element and passes it the component's input as it changes
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * The loader must be a stable function (declared at module level), because changing it remounts the figure. If
 * the figure cannot be loaded or WebGL is missing, the container is marked and the stylesheet shows a note;
 * the rest of the page keeps working.
 */

import { useEffect, useRef, type RefObject } from "react";
import { usePrefersReducedMotion } from "../lib/motion";
import type { SceneLoader } from "./loaders";
import type { SceneHandle } from "./scene-host";

export function useScene<Input>(load: SceneLoader<Input>, input: Input): RefObject<HTMLDivElement | null> {
  const containerRef = useRef<HTMLDivElement | null>(null);
  const handleRef = useRef<SceneHandle<Input> | null>(null);
  const inputRef = useRef(input);
  const isReduced = usePrefersReducedMotion();
  const reducedRef = useRef(isReduced);
  inputRef.current = input;
  reducedRef.current = isReduced;

  useEffect(() => {
    const container = containerRef.current;
    let isCancelled = false;
    if (container === null) {
      return undefined;
    }
    Promise.all([import("./scene-host"), load()])
      .then(([host, factory]) => {
        if (!isCancelled) {
          const handle = host.mountScene(container, factory, () => reducedRef.current);
          handle.setInput(inputRef.current);
          handleRef.current = handle;
        }
      })
      .catch(() => {
        container.dataset["webgl"] = "unavailable";
      });
    return () => {
      isCancelled = true;
      handleRef.current?.dispose();
      handleRef.current = null;
    };
  }, [load]);

  useEffect(() => {
    handleRef.current?.setInput(input);
  }, [input, isReduced]);

  return containerRef;
}
