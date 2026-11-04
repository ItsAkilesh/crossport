/*
 * File: site/src/three/camera.ts
 * Purpose: Work out how far a perspective camera must stand back for a figure to fit a container of any shape
 * Author: Akilesh
 * Date: 2026-11-04
 */

const DEGREES_TO_RADIANS = Math.PI / 180;

/** The distance at which a box of the given half extents, centred in front of the camera, fits the view. */
export function distanceToFit(fovDegrees: number, aspect: number, halfWidth: number, halfHeight: number): number {
  const tangent = Math.tan((fovDegrees * DEGREES_TO_RADIANS) / 2);
  return Math.max(halfHeight / tangent, halfWidth / (tangent * Math.max(aspect, 0.01)));
}
