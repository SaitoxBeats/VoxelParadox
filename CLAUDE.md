# Voxel Paradox Working Guide

Current snapshot: 2026-04-16.

Primary working config: `dev-release|x64`.
Detailed file inventory: `PROJECT_MAP.md`.
Historical roadmap: `PLAN.md` is now mostly context, because the gameplay refactor is already in place.

## Build And Tooling

- Use `bootstrap.ps1` from the repo root when the MSBuild props or vcpkg layout need to be regenerated.
- `bootstrap.ps1` creates `build/msbuild/VoxelParadox.Initial.props` and `build/msbuild/VoxelParadox.Settings.props`.
- The repo is configured for `x64-windows-static-md` through vcpkg.
- The current dependency set is:
  - `assimp`
  - `glad`
  - `glfw3`
  - `glm`
  - `lua`
  - `nlohmann-json`
  - `openal-soft`
  - `stb`
  - `imgui`
- Build the solution in `dev-release|x64` for normal work.
- `dev-release` keeps `NDEBUG` and `VP_ENABLE_DEV_TOOLS` together, which is the right balance for day-to-day iteration here.
- `Debug` is for deep debugging.
- `Release` is for final packaging and does not keep the dev tools path active.
- Build outputs land under `artifacts/bin/x64/<Configuration>/<ProjectName>/`.
- Exported staging layouts are produced by `tools/export_release.py`.
- Do not edit generated folders by hand: `.vs/`, `.idea/`, `artifacts/`, `build/msbuild/`, and `vcpkg_installed/`.

## Top-Level Layout

- `VoxelParadox.Engine` is the shared runtime engine and static library.
- It also compiles the shared world/biome/block source files from `VoxelParadox.Client/src/World/...`, so those implementations remain canonical even though the engine consumes them.
- `VoxelParadox.Client` is the game client and the main gameplay/content codebase.
- `VoxelParadox.BiomeMaker` is the biome preset editor.
- `VoxelParadox.ShaderEditor` is the block shader editor with hot reload.
- `VoxelParadox.Shared` is compatibility-only. Treat it as legacy forwarders, not a separate source-of-truth layer.
- `res/` is the development resource root.
- Staged exports mirror `res/` as `Resources/`.
- `tools/` contains support scripts for export, cleanup, and Visual Studio sync.
- `PROJECT_MAP.md` is the detailed generated tree if you need the exact inventory.

## Engine And App Flow

- Entry point: `VoxelParadox.Client/src/Core/runtime/entry/main.cpp` calls `VoxelParadox::runRuntimeApp()`.
- Startup order is settings, biome presets, window/OpenGL/ImGui/HUD, launcher, world session, audio, then the main runtime loop.
- `RuntimeAppInternal` is split across bootstrap, loop, death sequence, screenshot, and shared save helpers.
- `WorldLauncher` handles create, load, rename, delete, and exit before gameplay starts.
- `ENGINE` owns pause state, timing, viewport state, and performance counters.
- `AppPaths` resolves logical roots for both development `res/` layouts and staged `Resources/` layouts.
- `GameChat` is both chat and command console. It is not just a text overlay.

## Gameplay Architecture

- `Gameplay::Context` is the per-frame bridge between `Player`, `WorldStack`, `GameplayStatus::System`, audio, chat, portal UI, `EventQueue`, and `dt`.
- `Gameplay::RuntimeSystem` updates gameplay and then dispatches the frame event queue.
- `EventQueue` currently carries block broken/placed, item acquired/collected, XP changes, death/respawn, level up, portal creation, and universe enter/exit.
- `GameplayStatus::System` tracks playtime, XP, level, block/item acquisition, deaths, universe counts, and enemy counters. Current stats schema: `kStatsVersion = 7`.
- `Player` is still the public facade, but the behavior is split into:
  - `PlayerExperience`
  - `PlayerHealth`
  - `PlayerTargeting`
  - `PlayerPersistenceSystem`
  - movement
  - interaction
  - portal flow
  - item-script flow
- `BlockInteractionSystem` owns block breaking, block placing, drops (both hotbar-selected and inventory-held-item drops), and spawn-at-target helpers.
- `PortalInteractionSystem` owns portal preview, traversal, and nested-world transitions.
- `GameplayScriptContext` and `ItemUseContext` are the shared bridges for gameplay scripts and item Lua hooks.
- `ItemScriptSession` keeps a persistent Lua state for selected scripted items so `on_pickup`, `on_update`, and `on_use` can share state.
- `Enemy` gameplay currently flows through `EnemyDefinition`, `EnemySystem`, `EnemySpawnSystem`, and the runtime `EnemyType::Guy` module stack.
- `PlayerHotbar` tracks storage slots, craft slots, the held (cursor) slot, and selection index. `takeHeldSlot()` atomically returns and clears the cursor item for drop operations.
- `WorldStack` owns the active world hierarchy, traversal stack, preview world, render distance preset, named universes, and save/load flow.

## World, Data, And Persistence

- `BlockRegistry` and `ItemRegistry` are data-driven registries backed by `res/Assets/Models/Blocks/registry.json` and `res/Assets/Models/Items/registry.json`.
- Their definitions are richer than simple id lists:
  - block definitions carry properties, support rules, decoration rules, custom assets, and optional scripts
  - item definitions carry behavior kinds, tool metadata, declarative use actions, world preview data, and optional Lua scripts
- `BlockCatalog` and `ItemCatalog` provide the stable ids used by the rest of the code.
- `cloud_chunk` is an internal render-only block: white, transparent, non-solid, non-placeable, non-targetable, and drop-free. It is used only by the cloud renderer and must not be exposed as normal gameplay content.
- `BiomeRegistry` scans `res/World/BiomesPresets/*.fvbiome.json` and builds the selectable biome list.
- `VoxelGame::BiomePreset` is the inline-module biome asset format. Current preset format: `kBiomePresetFormatVersion = 3`.
- Current biome module families include perlin terrain, imported VOX files, grid patterns, Menger sponge, cave system, cellular noise, fractal noise, ridged noise, domain warped noise, tree generation, and cloud generation.
- `CloudGeneratorModule` data is parsed and preserved with biome presets, but it is a world-generation no-op. `ChunkGeneratorSource` skips it so terrain chunks, dirty chunk queues, and world modifications never receive cloud blocks.
- `WorldSaveService` owns world manifest, player data, and stats persistence.
- Current save versions are:
  - world manifest `2`
  - player data `10`
  - gameplay stats `7`
- World saves are stored as `world.dat`, `playerdata/player.dat`, `stats/stats_player.json`, and nested `universes/` folders.
- Runtime settings are stored in `%LOCALAPPDATA%\VoxelParadoxData\GameSettings.json` when available, otherwise in the workspace fallback.
- World save data lives under `%LOCALAPPDATA%\VoxelParadoxData\Saves\worlds` when available, otherwise in the workspace fallback.
- `ClientAssets` is the canonical place for stable asset paths. Prefer it over hardcoded literals.
- `ClientDefaults` holds runtime defaults such as title, OpenGL version, root seed, default font, window size, and tuning values.
- `AppPaths` keeps compatibility aliases alive for legacy asset and save paths. Use it instead of building paths manually.

## Assets And Content

- `res/Assets` currently contains `Audio`, `Config`, `Fonts`, `Models`, `Recipes`, `Shaders`, `texts`, `Textures`, and `Voxs`.
- `res/World` currently contains `BiomesPresets` and `Modules`.
- Audio definitions live in `res/Assets/Audio/Definitions`.
- Controls live in `res/Assets/Config/controls.lua`.
- The resource tree includes authored blocks, items, textures, models, shader sources, recipes, VOX imports, and biome presets.
- `AppPaths` supports both the development layout and the exported staged layout, so do not hardcode `res/` or `Resources/` in new code.

## Rendering And Editors

- `Renderer` owns the 3D frame, HUD overlays, item previews, block models, entity models, portal rendering, cloud rendering, dust particles, render targets, advanced lighting, and the death screen background.
- `CloudRenderer` owns the render-only biome cloud pass. It builds transient greedy-meshed cloud pages from `VoxelGame::Clouds`, frustum-culls visible pages, applies Low/Medium/High cloud quality caps, sorts them back-to-front, renders with premultiplied alpha and depth writes disabled, applies scene-depth soft-particle fading, and clears/recycles cached pages as presets, depths, modules, quality, or camera position change.
- Cloud visuals use the `cloud_chunk` material plus cloud-pass uniforms in `res/Assets/Shaders/block.frag` for softer silhouettes, scene-depth clipping fade, top/underside lighting, silver lining, fog blending, and cheaper low-quality breakup.
- `HUD` and `RuntimeUI` own the runtime UI layer, settings menu, save toast, developer UI, and cursor visibility.
- `hudInventoryMenu` receives `Player*` and `WorldStack*`. Clicking outside the inventory panel while holding items on the cursor drops the entire held stack into the world (Minecraft-style).
- `BiomeMaker` is the editor for `BiomePreset` documents and module libraries. Its cloud generator inspector edits the same module fields consumed by the client, and the preview viewport uses the shared cloud sampler and renderer.
- `ShaderEditor` is the block shader editor with hot reload, diagnostics, and a visual node graph.
- Both editors reuse the same engine bootstrap and shared runtime assets as the game client.
- The renderer and editors rely on the same block atlas, item textures, and shader resources as the game.

## ShaderEditor Node Graph

- The node graph panel in `ShaderEditor` is a visual authoring surface for the per-block shader snippet that `BlockRegistry::buildShaderSources()` wraps inside `sampleBlockMaterial_<id>(...)`, so it must remain compatible with the locals block.frag exposes at that call site: `base`, `uv`, `localUv`, `centeredUv`, `worldPos`, `worldNormal`, `faceNormal`, `viewDir`, `blockColor`, `blockAlpha`, `blockTexel`, `cell`, `cellHash`, `materialId`, plus globals `uTime` and `uBiomeTint`.
- `ShaderNodeGraph` (in `VoxelParadox.ShaderEditor/src/editor/shader_node_graph.{hpp,cpp}`) is the data model: nodes, typed pins (float/vec2/vec3/vec4), links, node catalog, GLSL codegen, and nlohmann-json persistence.
- Codegen always ends with `return makeSample(albedo, roughness, specular, emissive);` and writes scalar/vector conversions defensively, so the output stays valid inside the existing shader wrapper regardless of how the user wires types.
- `ShaderNodeEditor` (in the same folder) is the ImGui UI. It renders a custom canvas using `ImDrawList`, supports pan (middle mouse), zoom (mouse wheel around the cursor, clamped 0.35x - 2.5x), drag-to-connect pins, double-click on a connected input to disconnect, right-click context menus for adding nodes / node actions / link actions, and an inspector with per-input `Disconnect` buttons and constant editors.
- Undo/redo is snapshot-based on the graph's JSON serialization, with `Ctrl+Z` / `Ctrl+Shift+Z` / `Ctrl+Y` bindings and a bounded history (`kMaxHistory = 64`).
- Each block has its own node graph saved as `res/Assets/Models/Blocks/<block_id>/shader.nodegraph.json` next to the block's `shader.glsl`. When the preview block changes, the editor auto-loads that sidecar or resets to the default passthrough graph if no sidecar exists.
- `Apply to Block` runs codegen, writes the GLSL to the block's `shader.glsl`, writes the sidecar, and asks `BlockShaderSession` to reload; the existing fingerprint/hot-reload path then rebuilds and previews the shader without any additional plumbing.
- `Auto-Apply` rewrites the shader after each interaction ends (never mid-drag) so disk writes and reloads are not spammed during continuous edits.

## Advanced Lighting

- The advanced lighting path is part of the main block rendering pass in `Renderer`; it is not a separate pipeline.
- `GameSettings::advancedLightingEnabled` defaults to `true`, is persisted in `GameSettings.json`, and is exposed in the HUD settings menu.
- `Renderer::init()` compiles the data-driven block shader from `BlockRegistry::buildShaderSources()` first, then falls back to `res/Assets/Shaders/block.frag`, and finally to the embedded emergency shader in `VoxelParadox.Client/src/Renderer/render/core/renderer.cpp`.
- `BlockProperties` carries `emitsLight`, `lightColor`, and `lightRadius`; `BlockRegistry` reads those values from each block JSON `properties` block, and `block.hpp` exposes `isEmissive`, `getBlockLightColor`, and `getBlockLightRadius`.
- `Chunk::rebuildLightBlockInstances()` fills `Chunk::lightBlocks()` during meshing so the renderer can gather emissive blocks from generated chunks instead of rescanning raw voxels every frame.
- `Renderer::collectPointLights()` reserves slot 0 for the player torch, scans generated chunks near the camera, sorts emissive candidates by distance, and caps the upload at `kMaxPointLights = 32`.
- `Renderer::uploadPointLights()` writes the collected positions, colors, and radii into the block shader uniforms.
- `res/Assets/Shaders/block.frag` blends ambient light, directional diffuse, AO, point-light diffuse/specular, and emissive material glow. When advanced lighting is disabled, the renderer skips point-light accumulation but keeps the rest of the block shading path active.
- Keep `res/Assets/Shaders/block.frag` and the embedded fallback shader in `VoxelParadox.Client/src/Renderer/render/core/renderer.cpp` aligned whenever the lighting model changes.

## Working Rules

- Always prefer existing systems, registries, and helpers before creating a new one.
- Avoid hardcoding when a data file, registry, default constant, or path helper already exists.
- Keep C++ changes local to the owning subsystem.
- Preserve existing logic, names, log strings, and preprocessor guards unless the task explicitly asks for a behavior change.
- Keep include groups ordered as Standard, Third-party, Project, and use section markers for long files.
- All code comments in this repo should remain in English.
- When touching paths, go through `AppPaths` or the relevant `ClientAssets` constant.
- When touching gameplay, check whether the change belongs in `Player`, `Gameplay::RuntimeSystem`, a gameplay subsystem, or a registry before inventing a new layer.
- When touching UI, prefer the existing `HUD` / `RuntimeUI` / ImGui split instead of mixing them.
- If a system already exists for the job, extend it instead of adding a parallel one.

## Verification

- No dedicated automated test project is present in the solution.
- The normal verification path is a `dev-release|x64` build plus a targeted smoke test in the game client or the relevant editor.
- For packaging work, confirm the exported layout generated by `tools/export_release.py`.

## Historical Note

- The gameplay refactor checklist in `PLAN.md` is historical context now.
- The refactor is already applied in the codebase, so do not treat that checklist as active work.
- `VoxelParadox.Shared` should remain a compatibility layer unless there is a very specific migration reason to change it.

- Full project structure lives in `PROJECT_MAP.md`.
