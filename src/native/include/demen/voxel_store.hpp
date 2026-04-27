// =============================================================================
// demen/voxel_store.hpp — public API for the voxel storage subsystem.
//
// Planner-owned (invariant #4). Phase 1 Specialist implements against this
// header; changes require a deltas note back to the Planner.
//
// Design references:
//   - DESIGN.md §2.3   (storage, palette compression, chunk columns, aprons)
//   - DESIGN.md §2.3.1 (per-column metadata; ColumnCell schema)
//   - Appendix A       (on-disk region file format)
//   - Appendix G       (Layer 2 readiness API catalog — stub; grows here)
//   - Invariants #1, #2, #6, #7 (allocations, determinism, APIs, version byte)
// =============================================================================
#pragma once

#include "demen/abi.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Constants (match DESIGN.md §2.3 exactly; changing any of these is a
// world-format break and requires a bump to DEMEN_REGION_FORMAT_VERSION).
// -----------------------------------------------------------------------------
#define DEMEN_CHUNK_EDGE               32     // voxels per chunk edge
#define DEMEN_CHUNK_VOXELS             32768  // 32^3
#define DEMEN_REGION_EDGE_CHUNKS       32     // chunk columns per region edge
#define DEMEN_COLUMN_CELLS             1024   // 32 * 32 (x,z) entries
#define DEMEN_VOXEL_SIZE_METERS        2.0f   // DF-3D aesthetic, §1
#define DEMEN_REGION_FORMAT_VERSION    0x01   // Appendix A §A.5, invariant #7

// -----------------------------------------------------------------------------
// Result codes (blittable; C# enum mirrors this).
// -----------------------------------------------------------------------------
#define DEMEN_VS_OK                    0
#define DEMEN_VS_ERR_IO                1   // filesystem failure
#define DEMEN_VS_ERR_CORRUPT           2   // bad magic / bad checksum
#define DEMEN_VS_ERR_VERSION_MISMATCH  3   // §A.5 — refused cleanly (invariant #7)
#define DEMEN_VS_ERR_OOM               4   // native allocation failed
#define DEMEN_VS_ERR_OUT_OF_BOUNDS     5   // coords outside world
#define DEMEN_VS_ERR_NOT_LOADED        6   // chunk not resident
#define DEMEN_VS_ERR_LOCKED            7   // acquired elsewhere

// -----------------------------------------------------------------------------
// Opaque handles. Zero is always the null/invalid handle.
// -----------------------------------------------------------------------------
typedef uint64_t demen_world_t;
typedef uint64_t demen_chunk_t;     // a *loaded* chunk's handle; not a coordinate

// -----------------------------------------------------------------------------
// Per-column metadata (§2.3.1). Publicly visible because Layer 2 hydrology,
// the renderer (shadow/LOD), and the wave solver all read it by value each
// tick. 8 bytes; 1024 entries per column = 8 KiB.
// -----------------------------------------------------------------------------
#define DEMEN_COLUMN_FLAG_WAVE_RESEED    0x0001u
#define DEMEN_COLUMN_FLAG_MESH_REBUILD   0x0002u

typedef struct demen_column_cell {
    int16_t  terrain_top_y;        // Y of topmost solid voxel; INT16_MIN if none
    int16_t  water_surface_y;      // Y of topmost water voxel;  INT16_MIN if none
    uint16_t water_depth_voxels;   // 0 if no water
    uint16_t flags;                // DEMEN_COLUMN_FLAG_* mask
} demen_column_cell;

// -----------------------------------------------------------------------------
// World creation parameters. Blittable. `world_id` is the stable identifier
// written into Appendix A §A.3; seeding it from the managed side lets the
// determinism-replay tests (invariant #2) pin a world across runs.
// -----------------------------------------------------------------------------
typedef enum demen_world_scale {
    DEMEN_WORLD_FINITE_BOUNDED = 0,
    DEMEN_WORLD_STREAMING      = 1,
} demen_world_scale;

typedef struct demen_world_params {
    uint64_t          world_id;
    uint32_t          rng_seed;
    demen_world_scale scale;
    int32_t           bounds_min_chunk_x;   // ignored if streaming
    int32_t           bounds_min_chunk_z;
    int32_t           bounds_max_chunk_x;
    int32_t           bounds_max_chunk_z;
    int32_t           min_chunk_y;          // vertical stack bounds (both modes)
    int32_t           max_chunk_y;
} demen_world_params;

// -----------------------------------------------------------------------------
// Lifecycle.
// -----------------------------------------------------------------------------

/// Create a new world on disk at `region_dir`. Fails if the directory is
/// non-empty. On success, writes an empty header region file and returns a
/// live handle. Caller must eventually call demen_world_close.
DEMEN_API int demen_world_create(const char* region_dir,
                                 const demen_world_params* params,
                                 demen_world_t* out_world);

/// Open an existing world. Reads Appendix A header from the root region and
/// validates `version_byte == DEMEN_REGION_FORMAT_VERSION`. Returns
/// DEMEN_VS_ERR_VERSION_MISMATCH cleanly if not (invariant #7).
DEMEN_API int demen_world_open(const char* region_dir,
                               demen_world_t* out_world);

/// Flush all dirty chunks to disk and release resources. Safe to call on the
/// null handle (returns DEMEN_VS_OK).
DEMEN_API int demen_world_close(demen_world_t world);

/// Flush-only. Equivalent to close+reopen but cheaper. Used by the save-
/// point system and by the determinism harness between replay segments.
DEMEN_API int demen_world_flush(demen_world_t world);

// -----------------------------------------------------------------------------
// Voxel access — single-voxel calls for convenience; bulk calls for the
// hot paths. A specialist implementing this must not allocate per-call.
// -----------------------------------------------------------------------------

/// Read one voxel. `block_id` is the block-type id, 0 = air.
DEMEN_API int demen_world_get_voxel(demen_world_t world,
                                    int32_t x, int32_t y, int32_t z,
                                    uint16_t* out_block_id);

/// Write one voxel. Marks the containing chunk dirty for re-mesh and updates
/// the affected column's ColumnCell incrementally (§2.3.1 — invariant #6
/// APIs must stay correct after every edit, not only on save).
DEMEN_API int demen_world_set_voxel(demen_world_t world,
                                    int32_t x, int32_t y, int32_t z,
                                    uint16_t block_id);

/// Bulk fill an AABB with a single block id. Implementation may (and should)
/// take fast paths for whole-chunk writes, palette uniform, and adjacent-
/// chunk apron updates. Essential for world-gen throughput — per-voxel calls
/// for world-gen would be the textbook case §2.11 rule 2 is written for.
DEMEN_API int demen_world_fill_box(demen_world_t world,
                                   int32_t x0, int32_t y0, int32_t z0,
                                   int32_t x1, int32_t y1, int32_t z1,
                                   uint16_t block_id);

// -----------------------------------------------------------------------------
// Chunk access — for meshing, rendering, fluid sim. Handles are valid until
// release; the backing store will not unload an acquired chunk.
// -----------------------------------------------------------------------------

/// Acquire a loaded chunk by chunk-coordinates (not voxel coordinates).
/// Pins the chunk and its 1-voxel apron neighbours in memory until release.
DEMEN_API int demen_chunk_acquire(demen_world_t world,
                                  int32_t cx, int32_t cy, int32_t cz,
                                  demen_chunk_t* out_chunk);

DEMEN_API int demen_chunk_release(demen_chunk_t chunk);

/// Copy a chunk's 32^3 voxels (expanded from palette) into caller-provided
/// buffer. `out_buffer` must point to at least DEMEN_CHUNK_VOXELS uint16_t.
/// The copy is what makes this SAFE to expose across P/Invoke — no native
/// lifetime extends past the call. Meshing specialists working native-side
/// use a C++-only zero-copy accessor that is NOT part of this header.
DEMEN_API int demen_chunk_copy_voxels(demen_chunk_t chunk,
                                      uint16_t* out_buffer,
                                      uint32_t buffer_len);

// -----------------------------------------------------------------------------
// Column metadata (§2.3.1) and Layer 2 readiness APIs (invariant #6).
// These are point-queries — callers are expected to hit them millions of
// times per frame during sim. Implementation must be O(1) and allocation-
// free; the Phase 1 gate benchmarks it specifically.
// -----------------------------------------------------------------------------

DEMEN_API int demen_world_get_column_cell(demen_world_t world,
                                          int32_t x, int32_t z,
                                          demen_column_cell* out_cell);

/// Convenience: topmost-solid-voxel Y at (x, z), or INT16_MIN if empty.
/// Equivalent to reading column_cell.terrain_top_y but kept as a named API
/// because Layer 2 hydrology uses this verbatim in its flood-fill kernel
/// and a stable named entry point is a contract, not an implementation
/// detail.
DEMEN_API int demen_world_query_terrain_top_y(demen_world_t world,
                                              int32_t x, int32_t z,
                                              int16_t* out_y);

DEMEN_API int demen_world_query_water_depth(demen_world_t world,
                                            int32_t x, int32_t z,
                                            uint16_t* out_depth_voxels);

// -----------------------------------------------------------------------------
// Bulk column read — one region's worth (32 * 32 columns = 1024 cells each)
// for a chunk column range, used by the Layer 2 hydrology pre-pass and by
// the minimap renderer. Fills `out_cells` with stride-`out_stride_cells`
// rows of DEMEN_COLUMN_CELLS entries. Buffer size must be
// `(cx_max - cx_min + 1) * (cz_max - cz_min + 1) * DEMEN_COLUMN_CELLS`.
// -----------------------------------------------------------------------------

DEMEN_API int demen_world_copy_columns_bulk(
    demen_world_t world,
    int32_t cx_min, int32_t cz_min,
    int32_t cx_max, int32_t cz_max,
    demen_column_cell* out_cells,
    uint32_t buffer_len_cells);

#ifdef __cplusplus
}
#endif
