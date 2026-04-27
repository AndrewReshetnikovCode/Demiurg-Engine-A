# Phase gate tracker

| Phase | Goal | Status |
|---|---|---|
| 0 | Scaffolding | code-complete |
| 1 | Voxel storage | code-complete |
| 2 | Meshing + texture composition | code-complete |
| 3 | Renderer (Vulkan 1.3, bindless) | code-complete |
| 4 | Player & 5 toys | code-complete |
| 5 | Fluid sim — atmosphere | code-complete |
| 6 | Fluid sim — water & waves | code-complete |
| 7 | Weather | code-complete |
| 8 | Polish & OOTB | code-complete |

**All phases code-complete in sandbox. Gate records pending first real build.**

Sandbox-mode rules (docs/agent_pipeline/README.md) held throughout:
no push/PR, no external CI, human §3.5 gates disabled, benchmark gates
abolished per rule #7. Every subsystem has Planner-locked public headers,
its CMake target, a matching scope-locked implementation folder, and at
least one acceptance test skeleton. The invariants (#1 no managed allocs,
#2 determinism, #3 no ASM without benchmark, #4 public-header sign-off,
#6 Layer 2 readiness, #7 version-byte refusal, #8 optimization discipline)
remain as commit-checked rules for the debug pass.

## Known first-run debug items (carried forward from per-phase commits)

Each item is named so git-bisect can find the commit that introduced it.

* Phase 2 (`f0c5c49`): meshing's apron is filled with air because the
  voxel_store ABI does not yet expose (world, cx, cy, cz) from a chunk
  handle. Border quads between chunks render twice. Fix: add a
  `demen_chunk_info` entry and update `fill_chunk_view` to use it.
* Phase 2 (`f0c5c49`): every vertex ships `atlas_slot = 0`. Looks like a
  single-material world until the renderer-side block→slot callback is
  wired. Callback shape is prepared in `BlockToSlot`; needs the managed
  atlas lookup + a `demen_mesh_set_slot_callback` ABI.
* Phase 3 (`698f58c`): one sampler2D = whole atlas; shader does UV slot
  math. Expected to work but the first render might reveal that the
  shader's hard-coded `n_tiles = tile_height / atlas_height / 128` rule
  disagrees with the atlas the operator actually loads.
* Phase 3 (`698f58c`): pipeline cache is now persisted (Phase 8). On the
  very first run, cold launch will be dominated by pipeline creation —
  second run onward is warm. Invariant #5's 10 s gate is a warm-run
  target.
* Phase 4 (`1af21ac`): toys all display the chunk mesh because Layer 1
  ships no toy-specific meshes. Visually the scene looks like floating
  terrain blocks moving around. The game loop wiring is correct; only
  the mesh assets are missing.
* Phase 4 (`1af21ac`): Input.cs is an orbit stub. Player controller is
  live end-to-end but keyboard/mouse aren't wired. Needs a
  `demen_input_state` ABI.
* Phase 5/7 (`c02e181`, `905ca08`): macro-grid-to-air-grid coupling
  uses an approximate integer projection. First visual run will either
  look fine or show direction drift; fix is a two-line change.
* Phase 6 (`1344c56`): water CA calls `demen_world_get_voxel` per
  voxel — a mutex per call. B-WATER will flag it. Fix: a bulk water
  edit ABI in voxel_store.

Any and all of the above are in scope for the first debug session; none
change the architecture.
