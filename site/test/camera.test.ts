/*
 * File: site/test/camera.test.ts
 * Purpose: Tests for the camera framing helper
 * Author: Akilesh
 * Date: 2026-11-04
 */

import { describe, expect, it } from "vitest";
import { distanceToFit } from "../src/three/camera";

describe("distanceToFit", () => {
  it("stands back further for a narrower container, so the width still fits", () => {
    const wide = distanceToFit(40, 2, 8, 3);
    const narrow = distanceToFit(40, 0.5, 8, 3);
    expect(narrow).toBeGreaterThan(wide);
  });

  it("is limited by the height when the container is wide", () => {
    const tall = distanceToFit(40, 4, 1, 5);
    expect(tall).toBeCloseTo(5 / Math.tan((40 * Math.PI) / 360), 9);
  });

  it("never divides by zero for a collapsed container", () => {
    expect(Number.isFinite(distanceToFit(40, 0, 8, 3))).toBe(true);
  });
});
