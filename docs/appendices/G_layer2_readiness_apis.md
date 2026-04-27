# Appendix G — Layer 2 readiness API catalog

**Status:** Stub v0.1 (grows per subsystem). Must be finalised before Phase 5.
**Consumers:** future Layer 2 (hydrology), Layer 3+ (botany, fauna).
**Related:** DESIGN.md §1.4, §3.4 invariant #6.

---

## G.1 Purpose

Layer 1 exposes a **stable, cheap, allocation-free** point-query surface that
Layer 2 will depend on. Breaking or weakening any entry here after it ships
is a Planner-level decision — a Specialist cannot do it unilaterally
(invariant #6). The catalog below is the single source of truth for what
that surface is.

## G.2 Entries

| Entry point | Subsystem | Owns it since | Notes |
|---|---|---|---|
| `demen_world_query_terrain_top_y(world, x, z, &y)` | voxel_store | Phase 1 | Alias for ColumnCell.terrain_top_y |
| `demen_world_query_water_depth(world, x, z, &d)`   | voxel_store | Phase 1 | Voxels; 0 if dry |
| `demen_world_get_column_cell(world, x, z, &cell)`  | voxel_store | Phase 1 | Full ColumnCell — superset of the two above |
| `demen_world_copy_columns_bulk(...)`               | voxel_store | Phase 1 | Bulk read; required for Layer 2 region scans |
| `demen_fluid_query_wind(fluid, x, y, z, vec3)`       | fluid | Phase 5 (landed) | vec3 m/s at voxel; trilinear from air grid |
| `demen_fluid_query_temperature(fluid, x, y, z, &k)` | fluid | Phase 5 (landed) | Kelvin; advected field |
| `demen_fluid_query_rainfall(fluid, x, z, &mm_s)`    | fluid | Phase 7 (landed) | mm/s at weather macro-grid resolution |

## G.3 Guarantees

For every entry in G.2:

1. **O(1) cost** — no iteration, no allocation, no lock.
2. **Thread-safe for reads** — concurrent readers from any thread; writers
   (sim ticks, voxel edits) serialize on the relevant subsystem's tick.
3. **Deterministic output** — same world state + same args ⇒ same bytes out.
4. **Stable ABI** — signature change requires bumping `DEMEN_ABI_VERSION`
   (§2.1.2) and is a Planner decision.

## G.4 CI enforcement (invariant #6)

A nightly job from Phase 5 onward:

- Parses every entry in G.2 out of this file.
- Greps `src/native/include/demen/*.hpp` for the symbol.
- Fails if any entry is missing, has a different signature, or is not
  covered by at least one test under `tests/native/layer2_readiness/`.

## G.5 Open slots

- `demen_fluid_query_humidity` — likely added in Phase 7 once weather
  humidity exists as a field. Not committed yet.
- A bulk wind-sampling entry, if profiling shows per-voxel trilinear
  sampling is an actual bottleneck. Do *not* add speculatively —
  §2.11 rule #2 (batch) applies only when a benchmark proves it.
