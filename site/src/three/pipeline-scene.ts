/*
 * File: site/src/three/pipeline-scene.ts
 * Purpose: The pipeline figure: one small app moves through unpack, analyze, relink, rewrite, bind and bundle
 * Author: Akilesh
 * Date: 2026-11-04
 *
 * pipeline-model.ts says where each block should be at the chosen stage; this scene eases the blocks, their
 * colours, the lines between them and the extra pieces (container, markers, bundle frame) toward those targets.
 */

import {
  AmbientLight,
  BoxGeometry,
  BufferGeometry,
  Color,
  DirectionalLight,
  EdgesGeometry,
  Float32BufferAttribute,
  Group,
  IcosahedronGeometry,
  Line,
  LineBasicMaterial,
  LineSegments,
  Mesh,
  MeshBasicMaterial,
  MeshStandardMaterial,
  OctahedronGeometry,
  PerspectiveCamera,
  Scene,
  Vector3,
  type WebGLRenderer,
} from "three";
import { distanceToFit } from "./camera";
import { COLORS } from "./palette";
import { BLOCK_IDS, EDGES, stageTargets, type BlockColor, type BlockId } from "./pipeline-model";
import type { SceneContext, SceneController, SceneFrame } from "./scene-host";

export interface PipelineInput {
  readonly stage: number;
  readonly spread: number;
}

const FIELD_OF_VIEW = 36;
const FIT_HALF_WIDTH = 5.9;
const FIT_HALF_HEIGHT = 2.9;
const CAMERA_SIDE = 1.8;
const CAMERA_HEIGHT = 2.2;
const SMOOTHING = 6;
const PARALLAX_YAW = 0.25;
const PARALLAX_PITCH = 0.1;
const MARKER_PULSE = 0.25;
const MARKER_PULSE_RATE = 5;
const VISIBLE_THRESHOLD = 0.01;
const EDGE_COLOR = 0x94a3b8;
const EDGE_OPACITY = 0.55;
const CONTAINER_FILL_OPACITY = 0.15;
const BUNDLE_BASE_HALF_WIDTH = 3;
const MIN_BUNDLE_SCALE = 0.7;

const SIZES: Readonly<Record<BlockId, readonly [number, number, number]>> = {
  program: [1.6, 0.9, 0.7],
  libgreet: [1.1, 0.7, 0.7],
  libmath: [1.1, 0.7, 0.7],
  libc: [1, 0.6, 0.6],
  libx11: [1, 0.6, 0.6],
  hub: [0.55, 0.55, 0.55],
};

/** Where the rewrite markers sit: the block they belong to, and an offset from its centre. */
const MARKER_SPOTS: readonly (readonly [BlockId, number, number])[] = [
  ["program", -0.4, 0.6],
  ["program", 0.5, -0.55],
  ["libgreet", 0, 0.5],
  ["libmath", 0.3, -0.45],
];
const MARKER_DEPTH = 0.45;

const BLOCK_COLORS: Readonly<Record<BlockColor, number>> = {
  elf: COLORS.linux,
  pe: COLORS.windows,
  host: COLORS.stubbed,
  shim: COLORS.shim,
};

interface BlockView {
  readonly mesh: Mesh;
  readonly material: MeshStandardMaterial;
  readonly position: Vector3;
  readonly color: Color;
  opacity: number;
  scale: number;
}

interface EdgeView {
  readonly line: Line;
  readonly material: LineBasicMaterial;
  readonly from: BlockId;
  readonly to: BlockId;
}

/** Everything the figure draws, and the eased values it is currently showing. */
interface Figure {
  readonly scene: Scene;
  readonly camera: PerspectiveCamera;
  readonly group: Group;
  readonly views: ReadonlyMap<BlockId, BlockView>;
  readonly edges: readonly EdgeView[];
  readonly container: { readonly fill: Mesh; readonly fillMaterial: MeshBasicMaterial; readonly outline: LineSegments; readonly outlineMaterial: LineBasicMaterial };
  readonly bundle: { readonly outline: LineSegments; readonly material: LineBasicMaterial };
  readonly markers: { readonly meshes: readonly Mesh[]; readonly material: MeshBasicMaterial };
  readonly disposables: { dispose: () => void }[];
  containerOpacity: number;
  markerOpacity: number;
  bundleOpacity: number;
  edgeOpacity: number;
}

const targetPosition = new Vector3();
const targetColor = new Color();

function moveToward(current: number, target: number, ease: number): number {
  return current + (target - current) * ease;
}

function createBlocks(group: Group, disposables: { dispose: () => void }[]): ReadonlyMap<BlockId, BlockView> {
  const boxGeometry = new BoxGeometry(1, 1, 1);
  const hubGeometry = new IcosahedronGeometry(1, 1);
  disposables.push(boxGeometry, hubGeometry);
  const views = new Map<BlockId, BlockView>();
  for (const id of BLOCK_IDS) {
    const material = new MeshStandardMaterial({ roughness: 0.4, metalness: 0.1, transparent: true });
    if (id === "hub") {
      material.emissive = new Color(COLORS.shim);
      material.emissiveIntensity = 0.55;
    }
    disposables.push(material);
    const mesh = new Mesh(id === "hub" ? hubGeometry : boxGeometry, material);
    group.add(mesh);
    views.set(id, { mesh, material, position: new Vector3(), color: new Color(COLORS.linux), opacity: 0, scale: 1 });
  }
  return views;
}

function createEdges(group: Group, disposables: { dispose: () => void }[]): readonly EdgeView[] {
  return EDGES.map(([from, to]) => {
    const geometry = new BufferGeometry();
    geometry.setAttribute("position", new Float32BufferAttribute(new Float32Array(6), 3));
    const material = new LineBasicMaterial({ color: EDGE_COLOR, transparent: true });
    disposables.push(geometry, material);
    const line = new Line(geometry, material);
    group.add(line);
    return { line, material, from, to };
  });
}

function createFrames(group: Group, disposables: { dispose: () => void }[]): Pick<Figure, "container" | "bundle"> {
  const containerBox = new BoxGeometry(2.6, 3.4, 1.4);
  const containerEdges = new EdgesGeometry(containerBox);
  const fillMaterial = new MeshBasicMaterial({ color: EDGE_COLOR, transparent: true, depthWrite: false });
  const outlineMaterial = new LineBasicMaterial({ color: EDGE_COLOR, transparent: true });
  const bundleBox = new BoxGeometry(9, 4.2, 2.2);
  const bundleEdges = new EdgesGeometry(bundleBox);
  const bundleMaterial = new LineBasicMaterial({ color: COLORS.windows, transparent: true });
  disposables.push(containerBox, containerEdges, fillMaterial, outlineMaterial, bundleBox, bundleEdges, bundleMaterial);
  const fill = new Mesh(containerBox, fillMaterial);
  const containerOutline = new LineSegments(containerEdges, outlineMaterial);
  const bundleOutline = new LineSegments(bundleEdges, bundleMaterial);
  group.add(fill, containerOutline, bundleOutline);
  return {
    container: { fill, fillMaterial, outline: containerOutline, outlineMaterial },
    bundle: { outline: bundleOutline, material: bundleMaterial },
  };
}

function createMarkers(group: Group, disposables: { dispose: () => void }[]): Figure["markers"] {
  const geometry = new OctahedronGeometry(0.13);
  const material = new MeshBasicMaterial({ color: COLORS.marker, transparent: true });
  disposables.push(geometry, material);
  const meshes = MARKER_SPOTS.map(() => {
    const mesh = new Mesh(geometry, material);
    group.add(mesh);
    return mesh;
  });
  return { meshes, material };
}

function createFigure(): Figure {
  const scene = new Scene();
  const group = new Group();
  const light = new DirectionalLight(0xffffff, 1.5);
  light.position.set(2, 4, 6);
  scene.add(group, new AmbientLight(0xffffff, 0.8), light);
  const disposables: { dispose: () => void }[] = [];
  return {
    scene,
    camera: new PerspectiveCamera(FIELD_OF_VIEW, 1, 0.1, 60),
    group,
    views: createBlocks(group, disposables),
    edges: createEdges(group, disposables),
    ...createFrames(group, disposables),
    markers: createMarkers(group, disposables),
    disposables,
    containerOpacity: 0,
    markerOpacity: 0,
    bundleOpacity: 0,
    edgeOpacity: 0,
  };
}

/** Eases every shown value toward the targets for the chosen stage; `ease` of 1 jumps straight there. */
function advance(figure: Figure, input: PipelineInput, ease: number): void {
  const targets = stageTargets(input.stage, input.spread);
  for (const id of BLOCK_IDS) {
    const view = figure.views.get(id);
    const target = targets.blocks[id];
    if (view !== undefined) {
      view.position.lerp(targetPosition.set(target.position.x, target.position.y, target.position.z), ease);
      view.color.lerp(targetColor.set(BLOCK_COLORS[target.color]), ease);
      view.opacity = moveToward(view.opacity, target.opacity, ease);
      view.scale = moveToward(view.scale, target.scale, ease);
    }
  }
  figure.containerOpacity = moveToward(figure.containerOpacity, targets.container, ease);
  figure.markerOpacity = moveToward(figure.markerOpacity, targets.markers, ease);
  figure.bundleOpacity = moveToward(figure.bundleOpacity, targets.bundle, ease);
  figure.edgeOpacity = moveToward(figure.edgeOpacity, targets.edges, ease);
}

function drawBlocks(figure: Figure): void {
  for (const id of BLOCK_IDS) {
    const view = figure.views.get(id);
    const size = SIZES[id];
    if (view !== undefined) {
      view.mesh.position.copy(view.position);
      view.mesh.scale.set(size[0] * view.scale, size[1] * view.scale, size[2] * view.scale);
      view.material.color.copy(view.color);
      view.material.opacity = view.opacity;
      view.mesh.visible = view.opacity > VISIBLE_THRESHOLD;
    }
  }
}

function drawEdges(figure: Figure): void {
  for (const edge of figure.edges) {
    const start = figure.views.get(edge.from);
    const end = figure.views.get(edge.to);
    if (start !== undefined && end !== undefined) {
      const positions = edge.line.geometry.getAttribute("position");
      positions.setXYZ(0, start.position.x, start.position.y, start.position.z);
      positions.setXYZ(1, end.position.x, end.position.y, end.position.z);
      positions.needsUpdate = true;
      edge.material.opacity = EDGE_OPACITY * figure.edgeOpacity * Math.min(start.opacity, end.opacity);
    }
  }
}

function drawFrames(figure: Figure): void {
  const { container, bundle } = figure;
  container.fillMaterial.opacity = figure.containerOpacity * CONTAINER_FILL_OPACITY;
  container.outlineMaterial.opacity = figure.containerOpacity;
  container.fill.visible = container.outline.visible = figure.containerOpacity > VISIBLE_THRESHOLD;
  bundle.material.opacity = figure.bundleOpacity;
  bundle.outline.visible = figure.bundleOpacity > VISIBLE_THRESHOLD;
  const programX = figure.views.get("program")?.position.x ?? -BUNDLE_BASE_HALF_WIDTH;
  bundle.outline.scale.x = Math.max(Math.abs(programX) / BUNDLE_BASE_HALF_WIDTH, MIN_BUNDLE_SCALE);
}

function drawMarkers(figure: Figure, seconds: number): void {
  const pulse = 1 + MARKER_PULSE * Math.sin(seconds * MARKER_PULSE_RATE);
  figure.markers.material.opacity = figure.markerOpacity;
  figure.markers.meshes.forEach((marker, index) => {
    const spot = MARKER_SPOTS[index];
    const owner = spot === undefined ? undefined : figure.views.get(spot[0]);
    if (spot !== undefined && owner !== undefined) {
      marker.position.set(owner.position.x + spot[1], owner.position.y + spot[2], owner.position.z + MARKER_DEPTH);
    }
    marker.scale.setScalar(pulse);
    marker.visible = figure.markerOpacity > VISIBLE_THRESHOLD;
  });
}

export function createPipelineScene(context: SceneContext): SceneController<PipelineInput> {
  const figure = createFigure();
  let input: PipelineInput = { stage: 0, spread: 0.5 };
  let isSnapping = true;

  return {
    setInput: (next) => {
      input = next;
      isSnapping = context.isMotionReduced();
    },
    resize: (width, height) => {
      const aspect = width / Math.max(height, 1);
      figure.camera.aspect = aspect;
      figure.camera.position.set(CAMERA_SIDE, CAMERA_HEIGHT, distanceToFit(FIELD_OF_VIEW, aspect, FIT_HALF_WIDTH, FIT_HALF_HEIGHT));
      figure.camera.lookAt(0, 0, 0);
      figure.camera.updateProjectionMatrix();
    },
    render: (renderer: WebGLRenderer, frame: SceneFrame) => {
      advance(figure, input, isSnapping ? 1 : 1 - Math.exp(-SMOOTHING * frame.delta));
      isSnapping = false;
      drawBlocks(figure);
      drawEdges(figure);
      drawFrames(figure);
      drawMarkers(figure, frame.seconds);
      figure.group.rotation.y = context.pointer.x * PARALLAX_YAW;
      figure.group.rotation.x = context.pointer.y * PARALLAX_PITCH;
      renderer.render(figure.scene, figure.camera);
    },
    dispose: () => figure.disposables.forEach((item) => item.dispose()),
  };
}
