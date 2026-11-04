/*
 * File: site/src/three/loaders.ts
 * Purpose: Load each figure on demand, so three.js arrives in its own chunk while the page's text and controls are already usable
 * Author: Akilesh
 * Date: 2026-11-04
 */

import type { SceneFactory } from "./scene-host";
import type { GraphInput } from "./graph-scene";
import type { PipelineInput } from "./pipeline-scene";
import type { SkylineInput } from "./skyline-scene";

export type SceneLoader<Input> = () => Promise<SceneFactory<Input>>;

export const loadHeroScene: SceneLoader<undefined> = async () => (await import("./hero-scene")).createHeroScene;
export const loadPipelineScene: SceneLoader<PipelineInput> = async () => (await import("./pipeline-scene")).createPipelineScene;
export const loadGraphScene: SceneLoader<GraphInput> = async () => (await import("./graph-scene")).createGraphScene;
export const loadSkylineScene: SceneLoader<SkylineInput> = async () => (await import("./skyline-scene")).createSkylineScene;
