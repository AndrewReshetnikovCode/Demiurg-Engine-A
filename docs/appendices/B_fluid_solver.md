# Appendix B — Fluid solver pseudocode

**Status:** v1.0 (Phase 5–7 — implementation lands in
`src/native/fluid/`; this appendix pins the algorithm + determinism
rules the Specialist implements against).
**Consumers:** fluid subsystem; Benchmarker B-AIR / B-WATER / B-WEATHER /
B-DETERM suites.
**Related:** DESIGN.md §2.5, §2.8 (networking posture → determinism),
invariant #2 (replay hash), Appendix G (Layer 2 readiness queries).

---

## B.1 Atmospheric solver (§2.5.3)

Two-step Stam-style integrator on the atmospheric grid (1 cell per 4³
voxels = 8 m):

```
for each tick (§2.5.2 LOD zones):
    1. advect(air_grid, dt)                # semi-Lagrangian back-trace
    2. apply_weather_forcing(air_grid, weather_grid)   # §2.5.5
    3. project(air_grid, iters=4)          # Jacobi pressure projection
    4. integrate temperature passively with velocity
```

Cell state: `vec3 velocity`, `pressure`, `temperature`, `humidity`. Humidity
nearest-sampled (low spatial variance); temperature + velocity trilinear
sampled.

Jacobi iteration reads the previous-step pressure buffer, not the
in-progress one. That keeps the update order-independent — required for
determinism (§2.8).

## B.2 Water + wave solver (§2.5.4)

Two layers:

1. **Bulk water voxels** — cellular-automata flow on the voxel grid.
   At 5 Hz, each water voxel checks downhill neighbours in a fixed
   scanline order and redistributes up to 1 voxel of water. The
   iteration direction alternates each tick to avoid directional bias.

2. **Wave heightfield** — 2D grid, 4 m cells, overlaid on the top
   surface of each water voxel column. The height `h` obeys:
   ```
   h_tt = c² (h_xx + h_zz) - damping * h_t + forcing(wind)
   ```
   Discretised with a leapfrog integrator (two stored buffers: height
   and velocity). `c ≈ 3 m/s` and `damping ≈ 0.05` give pond-like waves
   without ringing.

Wind coupling: forcing = k * (wind · ∇h) — the wind pushes waves in its
direction, proportional to surface slope. `k ≈ 2e-4` is visual-only.

## B.3 Weather macro-grid (§2.5.5)

One cell per 16 chunks (~1 km). Fields: `pressure_pa`, `cloud_cover`,
`precipitation_mm_s`, `wind_mean[2]`.

Update at 1 Hz:
```
for each macro cell:
    pressure_pa  -> drift toward 101325 (long-term equilibrium)
    wind_mean    -> rotate by small amount; add neighbour blending later
    precipitation = max(0, (cloud_cover - 0.6) * 5)   # mm/s
```

Boundary forcing applied to the fine air grid's top layer once per macro
tick: air cell velocity blends 10% toward the overlapping macro cell's
`wind_mean`.

## B.4 LOD zones (§2.5.2)

| Zone | Radius | Tick rate | Source |
|---|---|---|---|
| Near | 0..4 chunks | 20 Hz | Full atmospheric grid |
| Mid  | 4..12       | 5 Hz  | Full atmospheric grid |
| Far  | 12+         | 1 Hz  | Weather macro-grid, interpolated |

A one-chunk overlap between near/mid (and mid/far) is stepped at both
rates and blended. Deliberately simple so the implementation doesn't
accumulate dead code for a feature the gate doesn't measure.

## B.5 Determinism (invariant #2)

All iteration orders are lexicographic (z, then y, then x). Jacobi uses
double-buffering rather than in-place updates; advection writes to a
fresh buffer; the wave integrator's two-step scheme already avoids
aliasing. No thread-schedule-dependent reductions. This is the minimum
structural set required for B-DETERM to produce a stable hash across
runs on the same commit.

## B.6 Layer 2 readiness (invariant #6)

Point-queries are O(1), allocation-free, thread-safe for concurrent
reads. Each maps world (x, y, z) to a grid cell via integer math only;
no search, no locking. See Appendix G for the catalog.

## B.7 What this appendix does NOT claim

- **Not a production CFD solver.** The energy equation is absent
  (§2.5.3). There is no vorticity confinement, no MAC grid, no
  variable-coefficient Poisson. Those are Layer 2/3 work when a
  physicist grade simulation becomes the product, not the differentiator.
- **Not thermodynamically consistent.** Temperature advection is
  passive; cooling is a simple Newton term. Good enough for "hot air
  rises" (§1.5 smoke toy); wrong for any chemistry coupling.
- **No implicit integration.** The CFL limit is `c * dt / cell < 1`.
  With `c = 3` and `cell = 4 m`, `dt < 1.33 s` — well inside the
  20 Hz step (50 ms) so we never hit it in Layer 1.
