# Appendix F — Material composition reference

**Status:** v1.0 (closed at Phase 1 gate; amendments require Planner sign-off).
**Consumers:** texture_composition subsystem; meshing (atlas slot lookup);
renderer (bindless descriptor layout).
**Related:** DESIGN.md §2.10, Appendix H (placeholder texgen).

---

## F.1 Purpose

A single source of truth for how a *material* — what a voxel face shows to
the player — is assembled from the three-part composition (§2.10):

```
material = (background, overlay, filter)
```

Phase 2 ships with **backgrounds only**; overlay and filter are format-supported
but unused until Layer 5 styles arrive. This appendix is the specification the
Phase 2 Specialist implements against, and the schema Layer 5 will extend
rather than replace.

## F.2 Block definition schema

One JSON file per block type, under `assets/blocks/<name>.json`. Phase 2 parses
every `*.json` in that folder at world load and hands the result to
`demen_atlas_create` (Appendix: `demen/texture_composition.hpp`).

```json
{
  "name":       "stone",
  "id":         1,
  "solid":      true,
  "opaque":     true,
  "material": {
    "background": "stone",
    "overlay":    null,
    "filter":     null
  },
  "layer2": {
    "is_water": false
  }
}
```

### F.2.1 Field reference

| Field | Type | Required | Notes |
|---|---|:---:|---|
| `name` | string | ✓ | Unique. Matches the `.texraw` stem in §F.3 when `overlay` and `filter` are null. |
| `id` | u16 | ✓ | Stored in chunk palettes (Appendix A §A.4.1). `0` is reserved for air; ids ≥ 65534 are reserved. |
| `solid` | bool | ✓ | Consumed by collision (§2.7) and apron occlusion (§2.4). |
| `opaque` | bool | ✓ | Drives pass routing — non-opaque blocks are deferred to the transparent pass (Phase 6). Phase 2 accepts `false` but refuses to mesh the block in `DEMEN_MESH_PASS_OPAQUE`. |
| `material.background` | string | ✓ | Stem of a `.texraw` file in `assets/textures/backgrounds/`. |
| `material.overlay` | string \| null | ✓ | Must be `null` in Phase 2; non-null values are rejected with `DEMEN_TC_ERR_CORRUPT`. Reserved for Layer 5 style-pools. |
| `material.filter` | string \| null | ✓ | Same rule as `overlay`. |
| `layer2.is_water` | bool | ✓ | Consumed by the voxel_store to populate `ColumnCell.water_surface_y` / `water_depth_voxels` (§2.3.1, invariant #6). |

### F.2.2 Validation at load

The loader rejects an asset set cleanly (no crash) if any of:

- Two definitions share an `id` or `name`.
- `material.background` points to a stem that has no matching `.texraw`.
- `overlay` or `filter` is non-null (§2.10 — Layer 5 territory).
- `id` == 0 (reserved) or `id` ≥ 65534.

Each is reported with the file path, the field, and the offending value.
No silent fallback. Matches invariant #7's "refuse cleanly, do not crash."

## F.3 Atlas slot assignment (Phase 2)

- One slot per **unique `(background, overlay, filter)` triple**. Two blocks
  that resolve to the same triple share a slot (§2.10).
- In Phase 2 `overlay` and `filter` are forced to `null`, so the triple
  collapses to `(background)`. Slot index is assigned by **alphabetical order
  of the background stem**, starting at 0. This is the ordering
  `demen_atlas_material_slot` returns.

The slot index is the only stable handle the renderer sees. Materials are
looked up by name only at load and hot-reload; the mesher writes
`atlas_slot` into every vertex (`demen_mesh_vertex.atlas_slot`) and the
shader does not know materials are composed at all.

## F.4 Composition algorithm (future-compatible)

The Phase 2 implementation short-circuits through the "background only" path,
but the full algorithm is pinned here so Layer 5 doesn't redesign it:

```
for each unique (background, overlay, filter) triple:
    img  = load_texraw(background)            // RGBA, tile_size × tile_size
    if overlay != null:
        img = alpha_over(img, load_texraw(overlay))
    if filter != null:
        img = apply_filter(img, filters[filter])   // HSV shift + tint preset
    atlas[slot] = img
```

Phase 2 implements only the first line. `alpha_over` and `apply_filter` are
placeholders until Layer 5 — adding them now would be speculative complexity
(§2.11 invariant #8; no gate criterion asks for them).

## F.5 Hot-reload behaviour (§2.10)

`demen_atlas_reload` re-reads the asset directory and recomposes the atlas.
Gate target: **< 50 ms** on reference hardware (Appendix E §E.4 B-TEXGEN).

Rules:

- Slot identity is stable across a reload: a material that kept its name
  keeps its slot index. New materials get the next free slot.
- Removed materials leave their slot **marked dead** (zeroed pixels + slot
  retired); the renderer's bindless table is not renumbered, because that
  would invalidate every mesh in the world.
- Reloads are atomic — the atlas pointer flips when the new pixel buffer
  is fully composed; no torn frames.

Phase 2 does not hot-reload block definitions; only the texture pixels
refresh. Block-id changes require a world restart and are explicitly out of
scope for Layer 1 (§1.3 "Save format migration").

## F.6 Layer 5 extension points (frozen in shape, not committed)

Layer 5 introduces *style-pools* — named bundles of
`(background, overlay, filter)` triples that a culture's builder picks from.
The schema extension is already shaped:

```json
// assets/styles/<culture>.json  — NOT LANDED UNTIL LAYER 5
{
  "name": "dwarven",
  "materials": [
    { "background": "quarried_stone", "overlay": "chisel_dwarven", "filter": "warm_grey" },
    { "background": "polished_stone", "overlay": "rune_dwarven",   "filter": "warm_grey" }
  ]
}
```

No code in Phase 2 reads this; the shape is recorded so the `texture_composition`
Specialist writes the Phase 2 loader against a schema that Layer 5 can grow
into (add a field) rather than rewrite.

## F.7 CI enforcement

- `tests/native/test_atlas_hot_reload.cpp` — B-TEXGEN suite (Appendix E §E.4):
  latency ≤ 50 ms; slot-stability invariant.
- A schema-validation test iterates every `assets/blocks/*.json`, confirming
  §F.2.1's field rules. Runs on every PR under the Phase 2 label.
- Any change to §F.2's field list bumps `DEMEN_ABI_VERSION` (§2.1.2) and is
  a Planner-sign-off decision (invariant #4).
