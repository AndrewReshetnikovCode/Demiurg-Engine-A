# Voxel Simulation Engine — Design Doc & AI Agent Pipeline

**Status:** Draft v2
**Target platform:** Windows (x64)
**Primary references:** Minecraft (for engine architecture), Dwarf Fortress (for world-scale aesthetics and interaction model)

---

## 1. Product Definition

### 1.1 Summary

A voxel-based simulation engine, differentiated from Minecraft by first-class simulation of wind, water, waves, and weather, and from Dwarf Fortress by being 3D and real-time. The engine must run smoothly out-of-the-box on Windows on mid-range 2022-era hardware, without user tuning.

The project is structured as a **layered simulation**, where each layer grounds the next and is shippable on its own. This document covers Layer 1 only.

### 1.2 Acceptance Criteria (MVP = Layer 1)

| Metric | Target |
|---|---|
| Frame rate | 60 FPS sustained (1% low ≥ 45 FPS) |
| Render distance | 12 chunks radius (~768 m at 2 m voxels), configurable up to 24 |
| Reference GPU | NVIDIA RTX 3060 / AMD RX 6600-class |
| Reference CPU | 6-core Zen 3 / Alder Lake-class |
| RAM budget | ≤ 6 GB at default render distance |
| Startup to playable | ≤ 10 seconds from cold launch |
| Atmospheric sim tick | 20 Hz near the player, 5 Hz mid-range, 1 Hz far (LOD) |
| Water sim tick | 5 Hz (bulk), 20 Hz (wave heightfield near player) |
| Chunk stream budget | ≤ 8 ms per frame, never blocking main thread |
| World scale | Configurable per-world (finite bounded or streaming infinite) |

"Out-of-the-box" is a hard constraint: no shader compile stutter on first run, no mandatory asset downloads, no config file tweaking.

### 1.3 Out of Scope (Layer 1)

- Multiplayer networking (but see §2.8 — architecture must not preclude it)
- Mod API / external scripting (§2.9)
- Console ports, Linux, Mac
- Ray tracing, global illumination
- Agents, NPCs, AI behavior (Layer 4+)
- Vegetation, hydrology, erosion (Layer 2–3)
- Hand-authored textures (see §2.10 — placeholders only)
- Save format migration (see §3.4 invariant #7)

### 1.4 Layered Roadmap

The project ships in five layers, each a standalone release. Each layer's simulation grounds the next — later layers consume state that earlier layers produce, never fake it.

| Layer | Content | Status |
|---|---|---|
| **1. Physics substrate** | Wind, water, waves, weather, terrain, 5 passive toys | **This document** |
| 2. Hydrology | Rivers, rainfall, flow, thunder, basic erosion | Future |
| 3. Botany | Grass, trees, roots; plants consume water and respond to wind | Future |
| 4. Fauna | Animals with simple drives; consume plants and water | Future |
| 5. Culture | Villagers with intent, style-pools per folk, emergent settlements | Future |

Design principle: **Layer N must expose queryable state that Layer N+1 needs.** For Layer 1, this means maintaining a heightmap and water-surface map per chunk column, and exposing point queries for wind vector, water depth, rainfall rate, and temperature. These APIs are load-bearing for Layer 2 and must be stable and cheap. See §3.4 invariant #6.

### 1.5 The Five Toys

Layer 1's simulation needs something to act on, or it reads as ambient decoration. The MVP ships with five passive entities, each proving a specific sim coupling:

| Toy | Proves |
|---|---|
| Sailboat | Wave buoyancy + wind force on sail |
| Flag / windsock | Directional wind sampling, visible at distance |
| Smoke source | Atmospheric advection, temperature coupling (hot air rises) |
| Windmill | Wind force application, mechanical feedback |
| Falling leaves | Per-voxel wind interpolation, drag, drift |

No goals, no survival loop, no damage, no inventory. Player can fly, walk, place voxels, spawn toys. That is the whole game in Layer 1 — deliberate, because scope is the #1 risk to a layered project and Layer 1's success criterion is "the physics feels alive," not "the game is fun."

---

## 2. Technical Architecture

### 2.1 Language Tiers

| Tier | Language | Responsibility |
|---|---|---|
| Orchestration | C# (.NET 8, NativeAOT where feasible) | Game loop, toy logic, UI, tooling, save/load orchestration, input, audio mixing |
| Core systems | C++20 | Chunk storage, meshing, fluid solver, renderer backend, spatial queries, serialization, texture composition |
| Hot kernels | C++ with SIMD intrinsics (AVX2 baseline, AVX-512 opt-in) | Greedy meshing, fluid advection/projection, palette encode/decode, frustum culling |
| Stretch (optional) | Hand-written x86-64 ASM | At most 1–2 kernels where a benchmark proves SIMD-C++ leaves ≥15 % on the table |

#### 2.1.1 Why not "ASM for FPS bottlenecks"

Hand-written assembly has been beaten by modern compiler autovectorization on anything longer than ~50 lines for over a decade. The real FPS wins in voxel engines come from data layout (SoA vs AoS), cache behavior, SIMD batching, and GPU-side work. The pipeline treats ASM as a post-profiling optimization, not a load-bearing architectural layer. If a kernel genuinely needs it, the agent must first produce a benchmark showing the SIMD-C++ version is the bottleneck, then write ASM alongside the C++ version (never replacing it) with both paths compiled in. See §3.4 invariant #3.

#### 2.1.2 Interop boundary

- C# ↔ C++: P/Invoke with blittable structs only across the boundary. No marshalling of managed objects into hot paths.
- Chunks, voxel data, and fluid grids live in native memory, owned by C++, exposed to C# as opaque handles + thin accessor functions.
- C# never allocates per-frame on the hot path. GC pressure is the #1 budget killer; the engine enforces a "zero allocations in game loop" invariant via a CI benchmark that fails the build if allocation count rises (§3.4 invariant #1).

### 2.2 Subsystem Map

```
┌─────────────────────────────────────────────────────────────┐
│                    C# Orchestration Layer                   │
│  Game loop │ Toys │ Input │ UI │ Save/load │ Audio          │
└──────────────────────────┬──────────────────────────────────┘
                           │ P/Invoke (blittable only)
┌──────────────────────────┴──────────────────────────────────┐
│                    C++ Core Layer                           │
│  ┌──────────────┐  ┌───────────────┐  ┌─────────────────┐   │
│  │ Voxel Store  │──│ Meshing       │──│ Renderer (Vk)   │   │
│  │ (chunks,     │  │ (greedy +     │  │ (bindless,      │   │
│  │  palettes,   │  │  SIMD)        │  │  indirect draw) │   │
│  │  heightmaps) │  └───────────────┘  └─────────────────┘   │
│  └──────┬───────┘                                           │
│         │                                                   │
│  ┌──────┴───────┐  ┌───────────────┐  ┌─────────────────┐   │
│  │ Fluid Solver │  │ Weather       │  │ Spatial Queries │   │
│  │ (NS-lite,    │──│ (pressure,    │  │ (raycast,       │   │
│  │  LOD tick)   │  │  temperature) │  │  frustum, AABB) │   │
│  └──────────────┘  └───────────────┘  └─────────────────┘   │
│  ┌──────────────┐                                           │
│  │ Texture      │                                           │
│  │ Composition  │                                           │
│  │ (bg+ovl+flt) │                                           │
│  └──────────────┘                                           │
└─────────────────────────────────────────────────────────────┘
```

### 2.3 Voxel Storage

- **Base voxel size: 2 meters.** Matches the project's DF-3D aesthetic — world detail lives in *what a tile contains* and in style composition, not in sub-meter geometric features.
- **Chunk size:** 32³ voxels = 64 m × 64 m × 64 m. Fits comfortably in L2 cache; a typical sandbox structure spans 2–4 chunks.
- **Palette compression per chunk:** 1-bit, 2-bit, 4-bit, or 8-bit indices into a per-chunk palette; uncompressed 16-bit fallback for chunks with >256 block types. Empty/uniform chunks stored as a single value.
- **Chunk columns** (stacks along Y) are the streaming unit.
- **Region files:** 32×32 chunk columns per region file, LZ4-compressed. Format in Appendix A.
- **Neighbor access:** meshing and sim need neighbor voxels; each loaded chunk stores a 1-voxel apron copied from neighbors (34³ storage, 32³ owned). Apron is invalidated when a neighbor edits a border voxel.

#### 2.3.1 Per-column metadata

Each chunk column maintains a dense 32×32 array of per-(x,z) records, updated incrementally on voxel edits. Required by Layer 1's wave solver and by Layer 2's hydrology.

```c
struct ColumnCell {
    int16_t  terrain_top_y;        // Y of topmost solid voxel, or sentinel
    int16_t  water_surface_y;      // Y of topmost water voxel, or sentinel
    uint16_t water_depth_voxels;   // voxels from bed to surface, 0 if none
    uint16_t flags;                // dirty bits: wave_reseed, mesh_rebuild, ...
};
// 8 bytes × 1024 = 8 KB per chunk column. Negligible.
```

Consumers:
- Wave solver (Layer 1) reads `water_surface_y` to know which cells to simulate.
- Renderer reads `terrain_top_y` for shadow culling and LOD selection.
- Layer 2 hydrology will read all fields.

### 2.4 Meshing

- **Algorithm:** Greedy meshing on quads, one pass per axis-direction (6 total), SIMD-accelerated face-occlusion test.
- **Transparent and translucent faces** rendered in a separate pass with back-to-front sort.
- **Mesh lifecycle:** meshes are owned by C++, uploaded to GPU once, kept resident until the chunk unloads. A dirty bit per chunk triggers re-meshing on a worker thread.
- **Texture atlasing:** single bindless texture array, one slot per *composited* material (§2.10). No per-frame descriptor updates.

### 2.5 Fluid & Atmospheric Simulation

The headline differentiator — gets the most design detail.

#### 2.5.1 Grids

Two fluid grids at different resolutions, because wind and water have different coherence scales and cost profiles.

| Grid | Resolution vs voxels | Cell size (at 2 m voxels) | Fields stored | Tick rate |
|---|---|---|---|---|
| Atmosphere | 1 cell per 4³ voxels | 8 m | velocity vec3, pressure, temperature, humidity | LOD (§2.5.2) |
| Water surface | 1 cell per 2³ voxels | 4 m | wave height offset, horizontal flow vec2 | 20 Hz near, 5 Hz mid |

Atmospheric grid resolution is set by a compile-time constant (`FLUID_AIR_DOWNSAMPLE`, default 4). Advanced users can rebuild with 2 or 8 for tighter or coarser sim. It is **not** a runtime setting — every sampler assuming a fixed ratio is simpler and cheaper.

#### 2.5.2 Wind LOD tick

Wind is the dominant cost. Instead of simulating the whole loaded world at 20 Hz, the sim operates in three concentric zones around the player:

| Zone | Radius (chunks) | Tick rate | Grid source |
|---|---|---|---|
| Near | 0–4 | 20 Hz | Full atmospheric grid |
| Mid | 4–12 | 5 Hz | Full atmospheric grid |
| Far | 12+ | 1 Hz | Weather macro-grid only (§2.5.5), interpolated |

Zone boundaries have a 1-chunk overlap where cells are stepped at both rates and blended, to prevent visible seams when a player moves between zones. This is cheap and the agent will want to skip it; making it explicit.

#### 2.5.3 Atmospheric solver

Semi-Lagrangian advection + a 4-iteration Jacobi pressure projection. No viscosity term, no full Poisson convergence, no energy equation — temperature is advected passively and updated by sources (sun, water evaporation) with a simple diffusion pass.

The force vector from the original spec is the grid velocity field. Pressure and temperature are stored per cell. Per-voxel effects (leaf sway, particle drift, smoke direction, flag orientation) sample the grid with trilinear interpolation.

Near-zone tick budget: ≤ 5 ms. Solver pseudocode in Appendix B.

#### 2.5.4 Water & waves

Water is represented in two layers:

1. **Bulk water voxels** — the volume of water in a region. Flow via a cellular-automata-style rule at 5 Hz. Fine-grained enough for rivers (Layer 2) when they arrive.
2. **Wave heightfield** — 2D grid overlaid on the top surface of bulk water, stored at the water-grid resolution (4 m cells). Wave height propagates with a linear wave equation (discrete Laplacian, two-step integrator), forced by local wind velocity and perturbed by entity impacts.

"Wave volume in a voxel" from the original spec maps to: each surface voxel has a wave-displaced height derived from the overlaid heightfield. Wave direction is not stored directly — it emerges from the heightfield gradient, which is what "determined by adjacent waves and wind" meant. Wind coupling is a forcing term proportional to (wind · normal) at the surface.

The wave solver reads `water_surface_y` from the per-column metadata (§2.3.1) to know which cells to simulate, rather than scanning for surfaces every tick.

#### 2.5.5 Weather

Weather is a macro-scale driver of the atmospheric field, not a separate system. A low-resolution weather grid (1 cell per 16 chunks, ~1 km at 2 m voxels) stores large-scale pressure gradients, precipitation rate, and cloud cover. Advects on its own clock (1 Hz) and applies as boundary forcing on the fine atmospheric grid.

The far-zone of wind LOD uses this grid directly.

### 2.6 Rendering

- **API:** Vulkan 1.3. DX12 was considered and rejected: Vulkan's validation layers and RenderDoc integration make it substantially easier for an AI agent to diagnose rendering bugs without a human eyeballing a frame.
- **Features used:** bindless descriptors, indirect draw, timeline semaphores, dynamic rendering (no render pass objects).
- **Swapchain:** triple-buffered, FIFO relaxed for low input latency.
- **Pipeline:** one main opaque pass, one transparent pass (water surface, windows, leaves), one post-process pass. No deferred shading in MVP.
- **Shader compilation:** all shaders precompiled to SPIR-V at build time and shipped; runtime pipeline cache primed on first launch to eliminate stutter.
- **Texture pipeline:** see §2.10.

### 2.7 Entities & Player

Scaled down from v1 since Layer 1 has only 5 toy types and a player.

- **Simple entity list** in C++, polymorphic via tagged union (no ECS framework needed for <100 entities). If Layer 4 arrives this gets promoted to component arrays; premature now.
- **Collision** against voxels uses swept AABB against the voxel grid.
- **Entities query the fluid grid** for drag and buoyancy. The sailboat, for instance, reads wave height + wind velocity at its position each frame. This is the canonical pattern Layers 3+ will follow.

### 2.8 Networking Posture

No netcode in MVP. But the sim is designed to be **deterministic given a fixed-rate tick and seeded RNG**, so that a future server-authoritative mode can replay inputs without divergence. This primarily constrains the fluid solver: the Jacobi iteration count and iteration order must be fixed, and no thread-schedule-dependent reductions are allowed.

### 2.9 Scripting

No external scripting. Toys are plain C# classes. Block definitions, material styles, and weather tables are data (JSON/TOML), hot-reloadable in dev builds. Gives modders a meaningful authoring surface (texture packs, style packs, block types) without exposing any code execution path.

### 2.10 Material Composition System

Textures are composed from three parts, rather than stored as one PNG per block type.

```
material: dwarven_wall_stone          # style-bound material (Layer 5 example)
  background: quarried_stone_2m       # base PNG, hand-authored (or placeholder)
  overlay:    chisel_pattern_dwarven  # optional second PNG, alpha-blended
  filter:     dwarven_warm_grey       # HSV shift + tint preset

material: granite                     # Layer 1 natural block, degenerate form
  background: raw_granite
  overlay: none
  filter: none
```

Composition happens once at texture-load time; result goes into a bindless texture atlas slot, keyed by (background, overlay, filter) triple. If two materials resolve to the same triple they share a slot. Shader does not know materials are composed.

**Layer 1 content requirement:** 8–10 background PNGs for natural materials (stone, dirt, grass, sand, water, wood, leaves, snow, gravel). No overlays, no filters yet — the format supports them but Layer 1 does not use them. **Placeholders generated by a checked-in build-time script** (Python or C++), producing 128×128 PNGs with distinguishable-but-readable palette per material. Not checkerboards; flat color + simple noise. The script is Layer 1's art pipeline.

**Hot-reload:** in dev builds, editing a block definition file triggers re-composition and atlas refresh without restarting.

**Future layers:** styles are added as data — a JSON file per culture (dwarven, elvish, etc.) defines a pool of (background, overlay, filter) triples that Layer 5 villagers pick from when building. Player-authored styles drop into the same folder. Not implemented in Layer 1, but the format supports them.

### 2.11 Optimization Philosophy

**Rule:** Choose the simplest approach that meets the FPS + memory budgets in §1.2. Prefer *structural* wins (data layout, algorithmic class, memory hierarchy, batching, work elimination) over *local* wins (tighter loops, hand-tuned instruction scheduling, micro-ops at already-fast call sites).

Concretely, this means the order in which a Specialist should try to make code faster or smaller is:

1. **Eliminate the work.** Can this be cached? Done once at load? Done off the hot path? Not done at all?
2. **Batch it.** Can N calls become 1 call over a span? Can N allocations become 1 arena? Can N draws become 1 indirect draw?
3. **Lay the data out for the cache.** SoA over AoS where traversal is column-wise; contiguous slabs over pointer-chased trees; chunks sized to fit L2 (§2.3 picked 32³ for this reason).
4. **Shrink the working set.** Palette compression beats faster decompression. Smaller structs beat faster iteration. Halving the chunk-apron duplication (§2.3) is worth more than any loop unroll.
5. **Move work to the GPU** when the GPU is idle and the bandwidth allows it (meshing stays on CPU, but culling and some particle work may migrate in Phase 8).
6. **Parallelise across cores** — per-chunk work is embarrassingly parallel; take it for free before micro-tuning any single thread's kernel.
7. **SIMD-vectorise the inner kernel** (AVX2 baseline). Only after 1–6.
8. **Hand-written ASM.** Only when (7) has been benchmarked and proven to leave ≥15 % on the table. See §2.1.1.

A 1–2 % win at a non-bottleneck is worth roughly zero, because the reviewer and the future maintainer pay the complexity cost every time they touch it. A 20 % win from picking a better data structure pays for itself on day one and compounds every time a new feature lands on top.

**In practice, this means the Specialist should never submit a PR whose *main* justification is "this is ~2 % faster."** If the PR is load-bearing for a gate criterion, say so. If it isn't, the simpler variant wins.

This is enforced at two levels:
- **Planner review** of every PR that adds non-obvious optimization complexity (see §3.4 invariant #8).
- **Benchmarker targets** in Appendix E are pass/fail on the whole-system number, not on per-function micro-benchmarks — a Specialist cannot "earn" complexity budget by showing a kernel is 2 % faster in isolation.

Corollary for simulation parameters: when two parameter values both clear the gate, **pick the cheaper one**. Example: if the air-grid pressure solver passes with 3 Jacobi iterations and also with 4, ship 3 (and document why 3 is enough, so a future Specialist doesn't "improve" it back to 4 thinking they're being careful).

---

## 3. AI Agent Pipeline

### 3.1 Architecture: Planner + Specialists + Benchmarker

One **Planner** agent owns the design doc, the task graph, and cross-cutting decisions. Specialist agents are spawned per task and scoped to a single subsystem. A dedicated **Benchmarker** agent gates phase transitions.

```
                    ┌─────────────────┐
                    │     Planner     │
                    │  (this document,│
                    │   task graph,   │
                    │   gates)        │
                    └────────┬────────┘
                             │ dispatches
         ┌───────────┬───────┼───────┬────────────┬──────────┐
         ▼           ▼       ▼       ▼            ▼          ▼
     ┌────────┐ ┌────────┐ ┌─────┐ ┌────────┐ ┌──────┐ ┌─────────┐
     │ Voxel  │ │Meshing │ │Fluid│ │Renderer│ │Entity│ │ Tooling │
     │Storage │ │        │ │ Sim │ │+Texcomp│ │ /Toy │ │ & Build │
     └────┬───┘ └────┬───┘ └──┬──┘ └────┬───┘ └──┬───┘ └────┬────┘
          │          │        │         │        │          │
          └──────────┴────────┴─────┬───┴────────┴──────────┘
                                    ▼
                          ┌──────────────────┐
                          │   Benchmarker    │
                          │  (runs perf      │
                          │  suites; gates   │
                          │  phase advance)  │
                          └──────────────────┘
```

Specialists cannot modify the design doc or add tasks — they can only *propose* deltas back to the Planner. The Benchmarker has no write access to code; its only power is to pass or fail a gate.

### 3.2 Specialist Contracts

Every specialist receives:
- **Interface spec** — C++ headers and C# P/Invoke signatures it must implement against, fixed by the Planner before dispatch.
- **Acceptance tests** — behavioral and performance tests that must pass.
- **Benchmark targets** — pulled from §1.2, scoped to this subsystem.
- **Scope lockout** — files outside the specialist's subsystem are read-only.

Every specialist returns:
- Implementation + unit tests.
- Benchmark results on a reference build.
- A "proposed deltas" document if the interface needed changes. Planner decides.

### 3.3 Phases & Gates

No specialist work on phase N+1 begins until phase N's Benchmarker gate passes.

| Phase | Goal | Gate criterion |
|---|---|---|
| 0. Scaffolding | Repo layout, build system (CMake + MSBuild), CI, minimal Vulkan window, placeholder texture script | Empty Vulkan window opens; CI green on Windows runner; placeholder textures generated at build time |
| 1. Voxel storage | Chunks, palettes, region files, column metadata, load/save | 10 k chunks generated and round-tripped through disk in <2 s; heightmap + water-surface map maintained correctly on edits; memory ≤ spec |
| 2. Meshing + texture composition | Greedy mesher, material composition pipeline, bindless atlas | 12-chunk-radius world meshes in <500 ms cold, <16 ms per dirty chunk; texture recomposition on hot-reload <50 ms |
| 3. Renderer | Vulkan backend, bindless, indirect draw, opaque + transparent passes | 60 FPS at 12-chunk radius on reference GPU with static scene |
| 4. Player & 5 toys | Swept AABB, camera, fly/walk controller, all 5 toys functional | Player can walk 10 k voxel path with no tunneling; each toy visible and animated; 60 FPS held |
| 5. Fluid sim — atmosphere | Coarse grid, advection, pressure projection, wind LOD zones, weather macro-grid | 20 Hz near-zone tick, ≤5 ms per step; LOD zone transitions seamless; determinism test passes |
| 6. Fluid sim — water & waves | Bulk water CA, wave heightfield, wind coupling | Visual correctness review (human gate); 60 FPS with full sim running; Layer 2 APIs exposed and stable |
| 7. Weather | Macro grid, cloud/precip, coupling back into atmosphere | Full weather cycle plays without FPS regression |
| 8. Polish & OOTB | Shader precompile, pipeline cache, first-run UX, installer, placeholder-texture distinguishability review | Cold launch to playable ≤10 s on clean Windows VM |

### 3.4 Cross-Cutting Invariants

Enforced by CI, not by agent discipline. Any specialist's PR is rejected automatically if it violates them:

1. **No managed allocations in the game loop.** Benchmarked by a test running 10 s of simulated gameplay; asserts GC gen-0 count is unchanged.
2. **Determinism.** A recorded-input replay test runs the sim for 60 s and hashes the world state; hash must match across runs on the same commit.
3. **No ASM without a benchmark.** Any `.asm` file in a PR requires an accompanying benchmark file showing the SIMD-C++ baseline it replaces and the delta (must be ≥15 %).
4. **Interface changes require Planner sign-off.** Any edit to a public header triggers a CI label that blocks merge until the Planner agent re-runs its dependency analysis.
5. **Out-of-the-box hold.** A CI job runs the shipped installer on a clean Windows Sandbox instance and asserts cold-launch-to-playable ≤10 s. Regressions block the phase gate.
6. **Layer 2 readiness.** From Phase 5 onward, point-query APIs (wind vector, water depth, rainfall rate, temperature) must be present, documented, and covered by tests. Removing or weakening their signature is a Planner-level decision; a specialist cannot do it unilaterally.
7. **Save header version byte.** Region file header must include a version byte from Phase 1 onward, even though no migration code exists pre-1.0. Missing byte blocks merge. Saves from mismatched engine builds are refused cleanly at load time with a clear user-facing error, not crashed on.
8. **Optimization complexity justification.** A PR that adds non-obvious optimization complexity (custom allocators, hand-rolled intrinsics, lock-free data structures, cache-line alignment hacks, loop-unrolling attributes, inline assembly, or anything similar) must include in the PR description: (a) the whole-system benchmark number before and after, (b) the simpler variant it replaces, (c) the gate criterion from §3.3 that *requires* the complexity. Missing any of those three blocks the PR. This operationalises §2.11 — the default is the simplest variant that clears the gate, and complexity is earned, not assumed.

### 3.5 Human-in-the-Loop Checkpoints

Four mandatory human gates:

- **End of Phase 0** — review repo structure, build system, and placeholder-texture generator before they become expensive to change.
- **End of Phase 3** — first time there is something to *look at*; visual/aesthetic calls happen here.
- **End of Phase 6** — fluid sim is the differentiator; human judgment on "does this look/feel right" cannot be automated.
- **End of Phase 8** — ship/no-ship.

### 3.6 Failure Modes & Escape Hatches

- **Specialist loops on a bug.** After 3 failed attempts on the same test, the specialist is killed and the task kicks back to the Planner with the failure trace. Planner decides: re-scope, re-spec, or escalate to human.
- **Benchmark target unreachable.** Specialist must file a "budget escalation" — a written argument for why the target is wrong. Planner adjudicates; human reviews if the Planner accepts a budget relaxation of >20 %.
- **Scope creep.** If a specialist's PR touches files outside its lockout, CI rejects it mechanically. No override.
- **Silent interface drift.** A nightly CI job diffs current public headers against the design doc's interface spec. Drift triggers a Planner review task.
- **Layer 2 API erosion.** If a specialist removes or weakens a Layer 2 readiness API to simplify Layer 1, CI rejects the PR regardless of test status.

---

## 4. Resolved Decisions (for audit)

Recorded so future readers and future agents understand why the doc looks the way it does, without re-litigating.

| Decision | Resolution | Rationale |
|---|---|---|
| World scale | Configurable per-world | User requirement |
| Fluid depth | Navier-Stokes-lite on coarse grid | User choice; matches cost budget |
| Agent architecture | Planner + specialists + dedicated Benchmarker | User choice + recommendation |
| Renderer | Vulkan | Better debugging tooling for AI agents |
| Scripting | No external scripting; data-driven content via JSON | User requirement |
| Base voxel size | 2 m | DF-3D aesthetic; bigger structural scale than Minecraft |
| Air grid | 1:4³ fixed, compile-time tunable | User proposed configurable; collapsed to compile-time to avoid runtime polymorphism in every sampler |
| Water grid | 1:2³ fixed | Fine enough for ponds |
| Wind cost scaling | LOD tick zones (20/5/1 Hz) | Simpler, cheaper, no interpolation seams |
| Save compatibility | None pre-1.0; version byte stub from Phase 1 | User choice (DF norm) |
| Genre | Layered roadmap; MVP = Layer 1 sandbox + 5 toys | Emerged from conversation |
| Heightmap / water-surface map | Maintained per chunk column | Serves Layer 1 wave solver and Layer 2 hydrology |
| Art pipeline | Material composition (bg + overlay + filter), pre-baked | User's proposal, refined |
| Layer 1 textures | Procedural placeholders via checked-in build script | User choice |
| Optimization philosophy | Structural wins first; 1–2 % micro-wins rejected by default | User choice; see §2.11 and invariant #8 |

---

## 5. Appendices (to be expanded per-phase)

- **A. Region file binary format** — chunk column layout, palette encoding, LZ4 framing, version byte placement. Needed before Phase 1.
- **B. Fluid solver pseudocode** — advection step, pressure projection, LOD zone blending, determinism notes. Needed before Phase 5.
- **C. Vulkan pipeline layouts** — descriptor sets, push constants, bindless table structure. Needed before Phase 3.
- **D. P/Invoke signature catalog** — every native function callable from C#, blittable struct definitions, ownership rules. Grows per phase.
- **E. Benchmark harness spec** — how the Benchmarker agent runs, what it measures, how gate decisions are recorded. Needed before Phase 0 closes.
- **F. Material composition reference** — JSON schema for blocks, backgrounds, overlays, filters; hot-reload behavior; Layer 5 style-pool format (stub). Needed before Phase 2.
- **G. Layer 2 readiness API catalog** — the point-query surface Layer 1 must expose. Needed before Phase 5.
- **H. Placeholder texture generator spec** — the build script's algorithm and output format. Needed in Phase 0.
