/*
 * File: site/src/three/graph-scene.ts
 * Purpose: The dependency figure: a program's libraries as nodes, with the shim as a hub that replaces the host libraries
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * Node colours say what Crossport does with each module: the program is white, bundled libraries are translated
 * (Windows cyan), host libraries the shim provides sit around the shim hub, and libraries nobody provides are
 * stubbed (grey). Hovering a name in the page's list highlights its node and the lines that reach it.
 */

import {
  AmbientLight,
  BufferGeometry,
  DirectionalLight,
  Float32BufferAttribute,
  Group,
  IcosahedronGeometry,
  LineBasicMaterial,
  LineSegments,
  Mesh,
  MeshStandardMaterial,
  PerspectiveCamera,
  Scene,
  SphereGeometry,
  TorusGeometry,
  type WebGLRenderer,
} from "three";
import type { ModuleKind, Program } from "../content/programs";
import { distanceToFit } from "./camera";
import { HUB_POSITION, layoutProgram, type Vec3 } from "./graph-layout";
import { COLORS } from "./palette";
import type { SceneContext, SceneController, SceneFrame } from "./scene-host";

export interface GraphInput {
  readonly program: Program;
  readonly highlightedId: string | null;
}

const FIELD_OF_VIEW = 36;
const FIT_HALF_WIDTH = 6.6;
const FIT_HALF_HEIGHT = 4;
const CENTRE_OFFSET_X = -1.05;
const NODE_RADIUS = 0.26;
const HIGHLIGHT_SCALE = 1.9;
const SPAWN_SECONDS = 0.5;
const SMOOTHING = 10;
const SWAY = 0.55;
const SWAY_RATE = 0.35;
const HALO_SPIN_RATE = 0.5;
const PARALLAX_YAW = 0.35;
const PARALLAX_PITCH = 0.12;
const HUB_RADIUS = 0.5;
const HALO_RADIUS_RATIO = 1.9;
const STUBBED_OPACITY = 0.5;
const MIN_NODE_SCALE = 0.001;

const KIND_COLORS: Readonly<Record<ModuleKind, number>> = {
  program: COLORS.program,
  translated: COLORS.windows,
  shim: COLORS.shim,
  stubbed: COLORS.stubbed,
};

type Segment = readonly [Vec3, Vec3];

interface NodeView {
  readonly id: string;
  readonly mesh: Mesh;
  readonly material: MeshStandardMaterial;
  age: number;
  scale: number;
}

interface GraphFigure {
  readonly scene: Scene;
  readonly camera: PerspectiveCamera;
  readonly pivot: Group;
  readonly content: Group;
  readonly sphere: SphereGeometry;
  readonly hubParts: readonly { dispose: () => void }[];
  readonly halo: Mesh;
  nodes: NodeView[];
  baseLines: LineSegments[];
  highlightLines: LineSegments | null;
  program: Program | null;
  positions: ReadonlyMap<string, Vec3>;
  highlightedId: string | null;
}

function lineSegments(segments: readonly Segment[], color: number, opacity: number): LineSegments {
  const geometry = new BufferGeometry();
  const values = segments.flatMap(([from, to]) => [from.x, from.y, from.z, to.x, to.y, to.z]);
  geometry.setAttribute("position", new Float32BufferAttribute(values, 3));
  return new LineSegments(geometry, new LineBasicMaterial({ color, transparent: true, opacity }));
}

function disposeLines(lines: LineSegments): void {
  lines.geometry.dispose();
  (lines.material as LineBasicMaterial).dispose();
}

function segmentsFor(program: Program, positions: ReadonlyMap<string, Vec3>, keep: (from: string, to: string) => boolean): Segment[] {
  const segments: Segment[] = [];
  for (const [from, to] of program.needs) {
    const start = positions.get(from);
    const end = positions.get(to);
    if (start !== undefined && end !== undefined && keep(from, to)) {
      segments.push([start, end]);
    }
  }
  return segments;
}

function createHub(content: Group): Pick<GraphFigure, "hubParts" | "halo"> {
  const hubGeometry = new IcosahedronGeometry(HUB_RADIUS, 1);
  const haloGeometry = new TorusGeometry(HUB_RADIUS * HALO_RADIUS_RATIO, 0.015, 8, 64);
  const hubMaterial = new MeshStandardMaterial({ color: COLORS.shim, emissive: COLORS.shim, emissiveIntensity: 0.6 });
  const haloMaterial = new MeshStandardMaterial({ color: COLORS.shim, emissive: COLORS.shim, emissiveIntensity: 0.4 });
  const hub = new Mesh(hubGeometry, hubMaterial);
  const halo = new Mesh(haloGeometry, haloMaterial);
  hub.position.set(HUB_POSITION.x, HUB_POSITION.y, HUB_POSITION.z);
  halo.position.copy(hub.position);
  halo.rotation.x = Math.PI / 2;
  content.add(hub, halo);
  return { hubParts: [hubGeometry, haloGeometry, hubMaterial, haloMaterial], halo };
}

function createFigure(): GraphFigure {
  const scene = new Scene();
  const pivot = new Group();
  const content = new Group();
  content.position.x = CENTRE_OFFSET_X;
  pivot.add(content);
  const key = new DirectionalLight(0xffffff, 1.6);
  key.position.set(3, 5, 7);
  scene.add(pivot, new AmbientLight(0xffffff, 0.8), key);
  return {
    scene,
    camera: new PerspectiveCamera(FIELD_OF_VIEW, 1, 0.1, 80),
    pivot,
    content,
    sphere: new SphereGeometry(NODE_RADIUS, 24, 16),
    ...createHub(content),
    nodes: [],
    baseLines: [],
    highlightLines: null,
    program: null,
    positions: new Map(),
    highlightedId: null,
  };
}

function clearProgram(figure: GraphFigure): void {
  for (const node of figure.nodes) {
    figure.content.remove(node.mesh);
    node.material.dispose();
  }
  for (const lines of figure.baseLines) {
    figure.content.remove(lines);
    disposeLines(lines);
  }
  figure.nodes = [];
  figure.baseLines = [];
}

function showProgram(figure: GraphFigure, program: Program): void {
  clearProgram(figure);
  figure.program = program;
  figure.positions = layoutProgram(program);
  for (const module of program.modules) {
    const position = figure.positions.get(module.id);
    if (position !== undefined) {
      const material = new MeshStandardMaterial({ color: KIND_COLORS[module.kind], roughness: 0.45, transparent: true });
      material.opacity = module.kind === "stubbed" ? STUBBED_OPACITY : 1;
      const mesh = new Mesh(figure.sphere, material);
      mesh.position.set(position.x, position.y, position.z);
      figure.content.add(mesh);
      figure.nodes.push({ id: module.id, mesh, material, age: 0, scale: 0 });
    }
  }
  const stubbedIds = new Set(program.modules.filter((module) => module.kind === "stubbed").map((module) => module.id));
  figure.baseLines = [
    lineSegments(segmentsFor(program, figure.positions, (_, to) => !stubbedIds.has(to)), 0x94a3b8, 0.4),
    lineSegments(segmentsFor(program, figure.positions, (_, to) => stubbedIds.has(to)), 0x64748b, 0.18),
  ];
  figure.content.add(...figure.baseLines);
}

function showHighlight(figure: GraphFigure, id: string | null): void {
  figure.highlightedId = id;
  if (figure.highlightLines !== null) {
    figure.content.remove(figure.highlightLines);
    disposeLines(figure.highlightLines);
    figure.highlightLines = null;
  }
  if (id !== null && figure.program !== null) {
    const segments = segmentsFor(figure.program, figure.positions, (from, to) => from === id || to === id);
    figure.highlightLines = lineSegments(segments, 0xffffff, 0.95);
    figure.content.add(figure.highlightLines);
  }
}

function animate(figure: GraphFigure, context: SceneContext, frame: SceneFrame): void {
  const isStill = context.isMotionReduced();
  const ease = isStill ? 1 : 1 - Math.exp(-SMOOTHING * frame.delta);
  for (const node of figure.nodes) {
    node.age += frame.delta;
    const target = figure.highlightedId === node.id ? HIGHLIGHT_SCALE : 1;
    const spawn = isStill ? 1 : Math.min(node.age / SPAWN_SECONDS, 1);
    node.scale += (target - node.scale) * ease;
    node.mesh.scale.setScalar(Math.max(node.scale * spawn, MIN_NODE_SCALE));
  }
  figure.pivot.rotation.y = (isStill ? 0 : Math.sin(frame.seconds * SWAY_RATE) * SWAY) + context.pointer.x * PARALLAX_YAW;
  figure.pivot.rotation.x = context.pointer.y * PARALLAX_PITCH;
  figure.halo.rotation.z = isStill ? 0 : frame.seconds * HALO_SPIN_RATE;
}

export function createGraphScene(context: SceneContext): SceneController<GraphInput> {
  const figure = createFigure();
  return {
    setInput: (next) => {
      if (figure.program !== next.program) {
        showProgram(figure, next.program);
        showHighlight(figure, next.highlightedId);
      } else if (figure.highlightedId !== next.highlightedId) {
        showHighlight(figure, next.highlightedId);
      }
    },
    resize: (width, height) => {
      const aspect = width / Math.max(height, 1);
      figure.camera.aspect = aspect;
      figure.camera.position.set(0, 0.6, distanceToFit(FIELD_OF_VIEW, aspect, FIT_HALF_WIDTH, FIT_HALF_HEIGHT));
      figure.camera.lookAt(0, 0, 0);
      figure.camera.updateProjectionMatrix();
    },
    render: (renderer: WebGLRenderer, frame: SceneFrame) => {
      animate(figure, context, frame);
      renderer.render(figure.scene, figure.camera);
    },
    dispose: () => {
      clearProgram(figure);
      showHighlight(figure, null);
      figure.sphere.dispose();
      figure.hubParts.forEach((part) => part.dispose());
    },
  };
}
