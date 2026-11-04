/*
 * File: site/src/three/hero-scene.ts
 * Purpose: The hero figure: blocks of code stream through a glass gate and change from Linux amber to Windows cyan as they cross
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * The blocks are instanced, so the whole figure is a handful of draw calls. Each block's colour and width depend
 * only on where it is along its lane, so the figure is a pure function of time.
 */

import {
  AmbientLight,
  BoxGeometry,
  Color,
  DirectionalLight,
  DynamicDrawUsage,
  Group,
  InstancedMesh,
  Mesh,
  MeshBasicMaterial,
  MeshStandardMaterial,
  Object3D,
  PerspectiveCamera,
  PointLight,
  Scene,
  type WebGLRenderer,
} from "three";
import { distanceToFit } from "./camera";
import { COLORS } from "./palette";
import type { SceneContext, SceneController, SceneFrame } from "./scene-host";

const FIELD_OF_VIEW = 38;
const ROWS = 3;
const DEPTHS = 3;
const LANES = ROWS * DEPTHS;
const BLOCKS_PER_LANE = 11;
const ROW_SPACING = 1.05;
const DEPTH_SPACING = 1.5;
const LANE_LENGTH = 17;
const GATE_HALF_WIDTH = 0.9;
const GATE_HEIGHT = 3.9;
const GATE_DEPTH = 5;
const SQUASH = 0.55;
const FIT_HALF_WIDTH = 8.4;
const FIT_HALF_HEIGHT = 3.4;
const CAMERA_HEIGHT = 3.4;
const CAMERA_SIDE = 3;
const PARALLAX_YAW = 0.28;
const PARALLAX_PITCH = 0.12;
const SWAY = 0.06;
const SWAY_RATE = 0.4;
const SEED = 7;

interface Block {
  readonly lane: number;
  readonly phase: number;
  readonly speed: number;
  readonly width: number;
  readonly height: number;
}

interface HeroFigure {
  readonly scene: Scene;
  readonly camera: PerspectiveCamera;
  readonly group: Group;
  readonly mesh: InstancedMesh;
  readonly blocks: readonly Block[];
  readonly disposables: { dispose: () => void }[];
}

const linux = new Color(COLORS.linux);
const windows = new Color(COLORS.windows);
const tint = new Color();
const placeholder = new Object3D();

/** A small deterministic generator, so the figure looks the same on every visit. */
function sequence(seed: number): () => number {
  let state = seed;
  return () => {
    state = (state * 1664525 + 1013904223) % 4294967296;
    return state / 4294967296;
  };
}

function makeBlocks(): readonly Block[] {
  const next = sequence(SEED);
  const blocks: Block[] = [];
  for (let lane = 0; lane < LANES; lane++) {
    const speed = 0.7 + (lane % ROWS) * 0.16 + Math.floor(lane / ROWS) * 0.08;
    for (let index = 0; index < BLOCKS_PER_LANE; index++) {
      blocks.push({
        lane,
        phase: (index / BLOCKS_PER_LANE) * LANE_LENGTH + next() * 0.5,
        speed,
        width: 0.5 + next() * 0.7,
        height: 0.32 + next() * 0.3,
      });
    }
  }
  return blocks;
}

function smoothstep(edge0: number, edge1: number, value: number): number {
  const t = Math.min(Math.max((value - edge0) / (edge1 - edge0), 0), 1);
  return t * t * (3 - 2 * t);
}

function addGate(group: Group, disposables: { dispose: () => void }[]): void {
  const post = new BoxGeometry(0.05, GATE_HEIGHT, GATE_DEPTH);
  const postMaterial = new MeshBasicMaterial({ color: COLORS.shim, transparent: true, opacity: 0.6 });
  const pane = new BoxGeometry(GATE_HALF_WIDTH * 2, GATE_HEIGHT, 0.02);
  const paneMaterial = new MeshBasicMaterial({ color: COLORS.shim, transparent: true, opacity: 0.09, depthWrite: false });
  disposables.push(post, postMaterial, pane, paneMaterial);
  for (const side of [-1, 1]) {
    const mesh = new Mesh(post, postMaterial);
    mesh.position.x = side * GATE_HALF_WIDTH;
    group.add(mesh);
  }
  group.add(new Mesh(pane, paneMaterial));
}

function createFigure(): HeroFigure {
  const scene = new Scene();
  const group = new Group();
  const key = new DirectionalLight(0xffffff, 1.6);
  key.position.set(3, 5, 6);
  scene.add(group, new AmbientLight(0xffffff, 0.75), key, new PointLight(COLORS.shim, 14, 9));
  const disposables: { dispose: () => void }[] = [];
  addGate(group, disposables);
  const blocks = makeBlocks();
  const geometry = new BoxGeometry(1, 1, 1);
  const material = new MeshStandardMaterial({ roughness: 0.4, metalness: 0.15 });
  const mesh = new InstancedMesh(geometry, material, blocks.length);
  mesh.instanceMatrix.setUsage(DynamicDrawUsage);
  group.add(mesh);
  disposables.push(geometry, material, mesh);
  return { scene, camera: new PerspectiveCamera(FIELD_OF_VIEW, 1, 0.1, 60), group, mesh, blocks, disposables };
}

/** Moves every block along its lane to where it is at `seconds`, colouring and squeezing it by how far through the gate it is. */
function placeBlocks(figure: HeroFigure, seconds: number): void {
  figure.blocks.forEach((block, index) => {
    const x = ((block.phase + seconds * block.speed) % LANE_LENGTH) - LANE_LENGTH / 2;
    const crossing = smoothstep(-GATE_HALF_WIDTH, GATE_HALF_WIDTH, x);
    const squash = 1 - SQUASH * Math.exp(-(x * x) / (GATE_HALF_WIDTH * GATE_HALF_WIDTH));
    const row = block.lane % ROWS;
    const depth = Math.floor(block.lane / ROWS);
    placeholder.position.set(x, (row - (ROWS - 1) / 2) * ROW_SPACING, (depth - (DEPTHS - 1) / 2) * DEPTH_SPACING);
    placeholder.scale.set(block.width * squash, block.height, 0.85);
    placeholder.updateMatrix();
    figure.mesh.setMatrixAt(index, placeholder.matrix);
    figure.mesh.setColorAt(index, tint.copy(linux).lerp(windows, crossing));
  });
  figure.mesh.instanceMatrix.needsUpdate = true;
  if (figure.mesh.instanceColor !== null) {
    figure.mesh.instanceColor.needsUpdate = true;
  }
}

export function createHeroScene(context: SceneContext): SceneController<undefined> {
  const figure = createFigure();
  return {
    setInput: () => undefined,
    resize: (width, height) => {
      const aspect = width / Math.max(height, 1);
      figure.camera.aspect = aspect;
      figure.camera.position.set(CAMERA_SIDE, CAMERA_HEIGHT, distanceToFit(FIELD_OF_VIEW, aspect, FIT_HALF_WIDTH, FIT_HALF_HEIGHT));
      figure.camera.lookAt(0, 0, 0);
      figure.camera.updateProjectionMatrix();
    },
    render: (renderer: WebGLRenderer, frame: SceneFrame) => {
      placeBlocks(figure, frame.seconds);
      const sway = context.isMotionReduced() ? 0 : Math.sin(frame.seconds * SWAY_RATE) * SWAY;
      figure.group.rotation.y = context.pointer.x * PARALLAX_YAW + sway;
      figure.group.rotation.x = context.pointer.y * PARALLAX_PITCH;
      renderer.render(figure.scene, figure.camera);
    },
    dispose: () => figure.disposables.forEach((item) => item.dispose()),
  };
}
