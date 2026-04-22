# Biome Cloud Visual And Performance Update Plan

## Summary
Upgrade the existing Biome cloud system so clouds look softer, avoid harsh clipping against terrain and blocks, and scale down cleanly on weak PCs. The update should keep clouds render-only, preset-scoped, deterministic, and independent from normal world chunk generation.

## Task List

1. **Add a dedicated cloud rendering quality profile**
   - Add a small runtime quality model for clouds with at least `Low`, `Medium`, and `High` presets.
   - Drive page budgets, shader cost, render distance, and LOD thresholds from the selected quality profile.
   - Keep existing per-biome cloud module settings as the authoring source; quality profiles only cap or simplify rendering cost.
   - Default weak-PC behavior should prefer stable frame time over dense clouds.

2. **Implement soft cloud clipping against scene depth**
   - Provide the cloud pass with access to scene depth after opaque world/entity/item rendering.
   - Add cloud shader uniforms for inverse projection/view data or another existing depth reconstruction path.
   - Fade cloud alpha as cloud fragments approach opaque scene geometry instead of allowing hard cuts.
   - Make the fade distance configurable through renderer constants or cloud quality settings.
   - Keep depth testing enabled and depth writes disabled for clouds.

3. **Switch the cloud pass to premultiplied alpha**
   - Update cloud shading so RGB is premultiplied by final alpha before output.
   - Use a premultiplied blending mode in the cloud pass.
   - Restore previous OpenGL blend state after rendering.
   - Verify the change does not affect normal block rendering, HUD rendering, held items, or portal previews.

4. **Create a cloud-specific shader path**
   - Stop relying on the generic block material look for clouds when a dedicated cloud material is active.
   - Add soft vertical shading: brighter tops, slightly darker undersides, and subtle horizon/fog blending.
   - Add a cheap rim/silver-lining effect based on view direction and light direction.
   - Add low-cost procedural breakup for close clouds and simplify it for distant or low-quality clouds.
   - Keep the authored `cloud_chunk` block as the internal material identity, but avoid making the final result look like a normal voxel block.

5. **Improve cloud silhouettes by profile**
   - Tune cloud profile sampling so each named type has a clearly different shape language.
   - Make `Cirrus` and `Cirrostratus` flatter, thinner, and more streak-like.
   - Make `Cumulus` thicker with rounded clusters and stronger vertical falloff.
   - Make `Stratus`, `Altostratus`, and `Nimbostratus` broader sheet-like layers.
   - Keep `Random` deterministic per seed, module, page, and layer.

6. **Add cloud LOD by distance**
   - Use high-detail voxel pages only near the camera.
   - Use simplified pages or lower sample density at medium distance.
   - Use very cheap impostor-like sheets or skipped detail at far distance.
   - Ensure LOD transitions fade or crossfade so clouds do not pop visibly.
   - Apply stricter LOD defaults in the `Low` quality profile.

7. **Add frustum and visibility culling**
   - Cull cloud pages outside the camera frustum before mesh build or draw.
   - Use page bounding boxes that include movement offset and layer height.
   - Keep sorting only for visible transparent pages.
   - Avoid generating or uploading pages that cannot contribute to the current frame.

8. **Reduce CPU mesh-generation cost**
   - Move expensive cloud page mesh generation away from the render hot path where practical.
   - Reuse cached page descriptors and avoid rebuilding pages when only the camera moved.
   - Avoid duplicate page sorting between the cloud sampler and renderer unless both orders are required.
   - Cap per-frame rebuilds more aggressively on low quality.
   - Track empty pages cheaply so repeatedly empty cloud pages are not resampled every frame.

9. **Simplify sampling for low-end rendering**
   - Add a cheaper 2.5D sampling mode for sheet-like clouds and low-quality rendering.
   - Prefer 2D noise plus vertical falloff for distant pages instead of full 3D FBM.
   - Reduce octave count and detail noise on low quality.
   - Keep generated results deterministic for the same preset and seed.

10. **Use render-distance fallback correctly**
    - Stop discarding the renderer-provided fallback render distance in cloud page collection.
    - Clamp module `renderDistance` against the active world/editor render distance and quality profile.
    - Make BiomeMaker preview and Client gameplay follow the same distance policy.

11. **Add debugging and tuning hooks**
    - Add optional dev-only counters for visible cloud pages, cached pages, rebuilt pages, drawn vertices, and skipped pages.
    - Add a simple dev overlay or log path only if an existing debug UI path already fits.
    - Keep these diagnostics disabled or hidden in normal gameplay.

12. **Update BiomeMaker controls**
    - Expose any new visual parameters that are meaningful for artists, such as softness or profile intensity.
    - Do not expose low-level renderer-only knobs unless they are useful for preset authoring.
    - Make inspector changes immediately refresh cloud preview caches.
    - Keep saved presets backward compatible with existing cloud modules.

13. **Update documentation**
    - Update `CLAUDE.md` with the dedicated cloud shader, soft clipping, LOD, and quality-profile behavior.
    - Update `PROJECT_MAP.md` if new files are added.
    - Document that clouds remain render-only and are still never written into terrain chunks or world modifications.

## Test Plan

1. **Build verification**
   - Run `msbuild VoxelParadox.slnx /p:Configuration=dev-release /p:Platform=x64`.
   - If BiomeMaker or Client executables are running and lock the linker output, close them or document the lock before rerunning.

2. **Visual clipping tests**
   - Fly clouds through mountains, trees, portals, and tall generated structures.
   - Confirm intersections fade softly instead of cutting with hard block-shaped edges.
   - Confirm clouds still respect opaque scene depth and do not draw through terrain.

3. **Shader quality tests**
   - Compare each cloud type in Client and BiomeMaker preview.
   - Confirm tops, undersides, rim lighting, and fog blending look stable at different camera angles.
   - Confirm low quality disables or simplifies expensive shader details without breaking alpha.

4. **Performance tests**
   - Test `Low`, `Medium`, and `High` cloud quality profiles while flying quickly across the map.
   - Confirm page rebuilds stay within budget and frame time stays stable on low settings.
   - Confirm distant clouds use cheaper LOD and do not generate unnecessary high-detail meshes.

5. **Correctness tests**
   - Confirm cloud modules still save and reload existing fields.
   - Confirm old presets without new fields load with safe defaults.
   - Confirm `cloud_chunk` remains non-placeable, non-targetable, non-solid, drop-free, and render-only.
   - Confirm cloud rendering still does not create world modifications, dirty terrain chunks, or generated terrain block data.

6. **Integration tests**
   - Confirm Client world rendering, portal previews, and BiomeMaker preview all use the same visual behavior.
   - Confirm HUD, held items, block highlights, particle rendering, and normal transparent blocks are not affected by cloud GL state changes.
   - Confirm switching worlds, depths, presets, or cloud quality clears or refreshes stale cloud cache data.

## Assumptions And Defaults
- The update improves the existing cloud system instead of replacing it with a fully volumetric raymarcher.
- Soft clipping should be implemented through scene-depth fading first because it directly targets the current clipping artifact.
- Low-end optimization should prioritize fewer pages, cheaper sampling, lower LOD, and fewer rebuilds before adding more expensive effects.
- Cloud quality settings may cap rendering cost but must not mutate biome preset data.
- Existing cloud presets remain valid and should render with reasonable defaults if new optional fields are missing.
