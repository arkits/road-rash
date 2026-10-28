# smooth-terrain-20261027

Task: owner request (October 27, 2026) after the quick-race playtest: the hill
textures look broken in the web port; fix the rendering even though the
original game draws them the same way.

Baseline: 75b8847. Build dirs: `build-agent-quickrace-20261027` (native
Debug), `build-agent-quickrace-web-20261027` (browser Release).

## Cause

Native terrain matches the original (see `quick-race-results-rider-20261027.md`).
The look comes from the original mapping: every terrain `DrawNode` maps the
whole texture onto its four corners. Hill textures are 256x32 strips
(`HILL11A`..`HILL22D` on Napa). Each hill band between two depth slices gets one
full copy, and near bands (`depth < 1234`) are subdivided by
`subdivide_hill_depths` into 2–8 quads that each restart the texture. Thin,
wide quads squash the tile vertically and stretch it sideways, with a seam per
quad. Mip selection by bounding box (`texture_mip_level`, smaller axis LOD)
then point-samples a 32-row texture at grazing angles, and its mips halve the
height to 4, 2 and 1 rows, which makes horizontal streaks.

## Change (native, not original; default on)

- `src/world_texture.{hpp,cpp}`: `attach_world_uv` inverts the scene
  projection at each corner's `view_depth` (`px = centre + (x - camera.x) *
  scale`, `py = (camera.height - y) * scale + horizon`, `scale` from the EXE
  depth table) and stores road-space texel coordinates: lateral offset from the
  road centre (interpolated `road_x` of the slice pair) and absolute route
  distance (`sample * 256 + depth offset`) plus height, at 4 world units per
  texel and 320 per route sample. The texture is anchored to the course, so it
  does not swim as the camera moves.
- `draw_world_quad`: two-triangle perspective raster (plane equations for
  1/z, u/z, v/z; half-open pixel-centre coverage; clip-aware for render
  bands) with wrapping inside the texture bounds. With a `WorldBlend` table
  it picks the mip from the short footprint axis (capped so a level keeps at
  least 8 rows) and averages up to 8 samples along the long axis in RGB,
  mapped back through a 15-bit nearest-palette table (`world_blend`, rebuilt
  when the palette bytes change).
- `CourseScene::draw_nodes` attaches world UVs to `hill_draw_nodes` output.
  `DrawNode::world_uv` is ignored unless the renderer is passed a blend table,
  so original-list replays and every existing raster path are unchanged.
- Indexed renderers (`render_nodes_perspective`, `render_nodes_threaded`) and
  the enhanced RGB path (`render_nodes_rgb_fog`, new `world` resolved-node kind
  with per-pixel fog from the interpolated depth) draw tagged hill nodes this way.
- `Settings::smooth_terrain` (`smooth_terrain=1` default, `0` restores the
  original raster), synced to `Game::smooth_terrain` per frame. Options →
  Graphics → Enhancements gains a "Smooth Terrain" On/Off row (12 rows now).
  It is independent of Visual Style presets.

Scope: hills (terrain type 4 bands) only. Shoulders, walls, plateaus, cross
roads, road and scenery keep the original mapping.

## Evidence

- Same-seed before/after frames (both runs started in the same second, so the
  launch `time()` jitter matches): `rr-game --course 0 --autopilot --ticks N
  --screenshot` with `smooth_terrain=0/1`, N = 6600, 7500, 7800, indexed and
  enhanced RGB (trilinear + fog 40). Squashed strips, per-quad seams and
  grazing-angle streaks are gone; the same textures, colours and fields stay.
  Images are disc-derived and stay local (scratchpad), not committed.
- `world-texture` ctest (synthetic textures): split strip has no gaps or
  double-drawn pixels and samples the undivided texels; uv wraps; a ground
  point keeps its texel across camera positions; untextured nodes stay
  untouched; with no blend table the original raster output is byte-identical;
  settings default/parse/round trip.
- `native_screens` and `frontend` tests updated for the extra Enhancements row.

## Checks

- Native Debug `build-agent-quickrace-20261027`: full build exit 0; full
  `ctest -j 6`: 122/122 passed, 0 skipped (454 s), including `world-texture`.
- Browser Release `build-agent-quickrace-web-20261027`: `tools/build_web.sh`
  exit 0, nine validated code files.

## Remaining

- Other terrain kinds (shoulders, walls, cross roads) could use the same mapping.
- Not deployed to road-rash.archit.xyz.
- The scale constants were tuned by eye on Napa (route samples 160/320/480
  compared); City, Sierra, Pacific Highway and Peninsula were checked at
  3000 ticks, original vs smooth, with no artefacts.

## Live processes / resources

None held.
