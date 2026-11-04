/*
 * File: site/src/three/skyline-scene.ts
 * Purpose: The status figure: a skyline with one column per milestone and one lit cell per finished task
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * The data comes from docs/progress.json through content/milestones.ts, so the figure shows what the README's
 * progress graphic shows. The selected milestone is drawn brighter; the page's list picks it.
 */

import {
  AmbientLight,
  BoxGeometry,
  Color,
  DirectionalLight,
  DynamicDrawUsage,
  Group,
  InstancedMesh,
  MeshStandardMaterial,
  Object3D,
  PerspectiveCamera,
  Scene,
  type WebGLRenderer,
} from "three";
import type { Milestone, MilestoneStatus } from "../content/milestones";
import { distanceToFit } from "./camera";
import { COLORS } from "./palette";
import type { SceneContext, SceneController, SceneFrame } from "./scene-host";
import { layoutSkyline, type Skyline } from "./skyline-layout";

export interface SkylineInput {
  readonly milestones: readonly Milestone[];
  readonly selectedId: string | null;
  readonly includePlanned: boolean;
}

const FIELD_OF_VIEW = 34;
const CELL_SIZE = 0.9;
const CELL_HEIGHT = 0.28;
const BASE_HEIGHT = 0.14;
const BASE_SIZE = 1.05;
const FIT_MARGIN_HEIGHT = 1.4;
const CAMERA_LIFT = 1.2;
const SWAY = 0.32;
const SWAY_RATE = 0.2;
const PARALLAX_YAW = 0.25;
const PARALLAX_PITCH = 0.08;
const DIM = 0.38;
const HIDDEN_SCALE = 0.0001;
/** More instances than the roadmap will ever need; a longer list is cut off rather than overflowing the buffers. */
const MAX_CELLS = 128;
const MAX_BASES = 24;

const BASE_COLORS: Readonly<Record<MilestoneStatus, number>> = {
  done: COLORS.done,
  next: COLORS.windows,
  planned: COLORS.pending,
};

interface SkylineFigure {
  readonly scene: Scene;
  readonly camera: PerspectiveCamera;
  readonly group: Group;
  readonly cells: InstancedMesh;
  readonly bases: InstancedMesh;
  readonly disposables: { dispose: () => void }[];
  readonly placeholder: Object3D;
  halfWidth: number;
  height: number;
  aspect: number;
}

function tint(base: number, isBright: boolean): Color {
  const color = new Color(base);
  return isBright ? color : color.multiplyScalar(DIM);
}

function createFigure(): SkylineFigure {
  const scene = new Scene();
  const group = new Group();
  const key = new DirectionalLight(0xffffff, 1.7);
  key.position.set(4, 8, 6);
  scene.add(group, new AmbientLight(0xffffff, 0.8), key);
  const cellGeometry = new BoxGeometry(CELL_SIZE, CELL_HEIGHT, CELL_SIZE);
  const baseGeometry = new BoxGeometry(BASE_SIZE, BASE_HEIGHT, BASE_SIZE);
  const material = new MeshStandardMaterial({ roughness: 0.4, metalness: 0.1 });
  const cells = new InstancedMesh(cellGeometry, material, MAX_CELLS);
  const bases = new InstancedMesh(baseGeometry, material, MAX_BASES);
  cells.instanceMatrix.setUsage(DynamicDrawUsage);
  bases.instanceMatrix.setUsage(DynamicDrawUsage);
  group.add(cells, bases);
  return {
    scene,
    camera: new PerspectiveCamera(FIELD_OF_VIEW, 1, 0.1, 80),
    group,
    cells,
    bases,
    disposables: [cellGeometry, baseGeometry, material, cells, bases],
    placeholder: new Object3D(),
    halfWidth: 6,
    height: 3,
    aspect: 1,
  };
}

function frameCamera(figure: SkylineFigure): void {
  const distance = distanceToFit(FIELD_OF_VIEW, figure.aspect, figure.halfWidth, figure.height / 2 + FIT_MARGIN_HEIGHT);
  figure.camera.aspect = figure.aspect;
  figure.camera.position.set(0, figure.height + CAMERA_LIFT, distance);
  figure.camera.lookAt(0, figure.height / 2, 0);
  figure.camera.updateProjectionMatrix();
}

function hideUnused(figure: SkylineFigure, mesh: InstancedMesh, used: number, capacity: number): void {
  figure.placeholder.position.set(0, 0, 0);
  figure.placeholder.scale.setScalar(HIDDEN_SCALE);
  figure.placeholder.updateMatrix();
  for (let index = used; index < capacity; index++) {
    mesh.setMatrixAt(index, figure.placeholder.matrix);
  }
}

function writeInstances(figure: SkylineFigure, skyline: Skyline, selectedId: string | null): void {
  const { placeholder } = figure;
  const isLit = (id: string): boolean => selectedId === null || selectedId === id;
  const cells = skyline.cells.slice(0, MAX_CELLS);
  const bases = skyline.bases.slice(0, MAX_BASES);
  placeholder.scale.setScalar(1);
  cells.forEach((cell, index) => {
    placeholder.position.set(cell.x, BASE_HEIGHT + cell.y + CELL_HEIGHT / 2, 0);
    placeholder.updateMatrix();
    figure.cells.setMatrixAt(index, placeholder.matrix);
    figure.cells.setColorAt(index, tint(cell.isDone ? COLORS.done : COLORS.pending, cell.isDone && isLit(cell.milestoneId)));
  });
  bases.forEach((base, index) => {
    placeholder.position.set(base.x, BASE_HEIGHT / 2, 0);
    placeholder.updateMatrix();
    figure.bases.setMatrixAt(index, placeholder.matrix);
    figure.bases.setColorAt(index, tint(BASE_COLORS[base.status], isLit(base.milestoneId)));
  });
  hideUnused(figure, figure.cells, cells.length, MAX_CELLS);
  hideUnused(figure, figure.bases, bases.length, MAX_BASES);
  for (const mesh of [figure.cells, figure.bases]) {
    mesh.instanceMatrix.needsUpdate = true;
    if (mesh.instanceColor !== null) {
      mesh.instanceColor.needsUpdate = true;
    }
  }
}

function showInput(figure: SkylineFigure, input: SkylineInput): void {
  const skyline = layoutSkyline(input.milestones, input.includePlanned);
  writeInstances(figure, skyline, input.selectedId);
  figure.halfWidth = skyline.halfWidth;
  figure.height = skyline.height + BASE_HEIGHT;
  frameCamera(figure);
}

export function createSkylineScene(context: SceneContext): SceneController<SkylineInput> {
  const figure = createFigure();
  return {
    setInput: (input) => showInput(figure, input),
    resize: (width, height) => {
      figure.aspect = width / Math.max(height, 1);
      frameCamera(figure);
    },
    render: (renderer: WebGLRenderer, frame: SceneFrame) => {
      const sway = context.isMotionReduced() ? 0 : Math.sin(frame.seconds * SWAY_RATE) * SWAY;
      figure.group.rotation.y = sway + context.pointer.x * PARALLAX_YAW;
      figure.group.rotation.x = context.pointer.y * PARALLAX_PITCH;
      renderer.render(figure.scene, figure.camera);
    },
    dispose: () => figure.disposables.forEach((item) => item.dispose()),
  };
}
