/*
 * File: site/src/three/scene-host.ts
 * Purpose: Mount a three.js scene in a container: sizing, a render loop that pauses off-screen, reduced motion and clean disposal
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * Scenes describe what to draw; the host owns the renderer, the canvas and the loop. The loop runs only while
 * the container is on screen and the tab is visible. With reduced motion requested it draws single frames (on
 * resize and when the scene's input changes) instead of animating. If WebGL is unavailable the container is
 * marked so the stylesheet can show a still poster, and the rest of the page keeps working.
 */

import { WebGLRenderer } from "three";

const MAX_PIXEL_RATIO = 2;
/** The time a still frame is drawn at, chosen so the figures are in a readable pose. */
const STILL_SECONDS = 3;
const MAX_FRAME_DELTA = 0.1;

export interface Pointer {
  /** Pointer position over the container, from -1 (left, top) to 1 (right, bottom); 0 when it has not moved. */
  x: number;
  y: number;
}

export interface SceneFrame {
  readonly seconds: number;
  readonly delta: number;
}

export interface SceneContext {
  readonly pointer: Readonly<Pointer>;
  readonly isMotionReduced: () => boolean;
}

export interface SceneController<Input> {
  readonly setInput: (input: Input) => void;
  readonly resize: (width: number, height: number) => void;
  readonly render: (renderer: WebGLRenderer, frame: SceneFrame) => void;
  readonly dispose: () => void;
}

export type SceneFactory<Input> = (context: SceneContext) => SceneController<Input>;

export interface SceneHandle<Input> {
  readonly setInput: (input: Input) => void;
  readonly dispose: () => void;
}

function createRenderer(canvas: HTMLCanvasElement): WebGLRenderer | null {
  try {
    const renderer = new WebGLRenderer({ canvas, antialias: true, alpha: true });
    renderer.setClearColor(0x000000, 0);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, MAX_PIXEL_RATIO));
    return renderer;
  } catch {
    return null;
  }
}

function trackPointer(container: HTMLElement, pointer: Pointer): () => void {
  const onMove = (event: PointerEvent): void => {
    const box = container.getBoundingClientRect();
    pointer.x = ((event.clientX - box.left) / Math.max(box.width, 1)) * 2 - 1;
    pointer.y = ((event.clientY - box.top) / Math.max(box.height, 1)) * 2 - 1;
  };
  container.addEventListener("pointermove", onMove, { passive: true });
  return () => container.removeEventListener("pointermove", onMove);
}

export function mountScene<Input>(
  container: HTMLElement,
  factory: SceneFactory<Input>,
  isMotionReduced: () => boolean,
): SceneHandle<Input> {
  const canvas = document.createElement("canvas");
  canvas.setAttribute("aria-hidden", "true");
  const renderer = createRenderer(canvas);
  if (renderer === null) {
    container.dataset["webgl"] = "unavailable";
    return { setInput: () => undefined, dispose: () => undefined };
  }
  container.append(canvas);
  const pointer: Pointer = { x: 0, y: 0 };
  const controller = factory({ pointer, isMotionReduced });
  let isVisible = false;
  let frameRequest = 0;
  let lastSeconds = 0;

  const draw = (now: number): void => {
    frameRequest = 0;
    const seconds = isMotionReduced() ? STILL_SECONDS : now / 1000;
    const delta = Math.min(Math.max(seconds - lastSeconds, 0), MAX_FRAME_DELTA);
    lastSeconds = seconds;
    controller.render(renderer, { seconds, delta });
    if (isVisible && !document.hidden && !isMotionReduced()) {
      frameRequest = requestAnimationFrame(draw);
    }
  };
  const requestFrame = (): void => {
    if (frameRequest === 0) {
      frameRequest = requestAnimationFrame(draw);
    }
  };
  const sizeObserver = new ResizeObserver(() => {
    const { clientWidth: width, clientHeight: height } = container;
    renderer.setSize(width, height, false);
    controller.resize(width, height);
    requestFrame();
  });
  const visibilityObserver = new IntersectionObserver((entries) => {
    isVisible = entries.some((entry) => entry.isIntersecting);
    if (isVisible) {
      requestFrame();
    }
  });
  const onVisibilityChange = (): void => requestFrame();
  const stopPointer = trackPointer(container, pointer);
  sizeObserver.observe(container);
  visibilityObserver.observe(container);
  document.addEventListener("visibilitychange", onVisibilityChange);

  return {
    setInput: (input) => {
      controller.setInput(input);
      requestFrame();
    },
    dispose: () => {
      cancelAnimationFrame(frameRequest);
      sizeObserver.disconnect();
      visibilityObserver.disconnect();
      document.removeEventListener("visibilitychange", onVisibilityChange);
      stopPointer();
      controller.dispose();
      renderer.dispose();
      canvas.remove();
    },
  };
}
