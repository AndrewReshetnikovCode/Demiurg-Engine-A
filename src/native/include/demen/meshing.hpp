// =============================================================================
// demen/meshing.hpp — public API for the meshing subsystem (§2.4).
// Planner-owned (invariant #4). Phase 2 Specialist implements against this
// header; changes require a deltas note back to the Planner.
//
// Design references:
//   - DESIGN.md §2.4   (greedy meshing, apron, bindless atlas)
//   - DESIGN.md §2.3   (chunk layout; the apron this header consumes)
//   - Appendix E §E.3  (B-MESH suite targets: <500 ms cold / <16 ms dirty)
//   - Invariants #1 (no managed allocations), #2 (determinism),
//                #6 (terrain_top_y consumer), #8 (optimization discipline)
//
// Phase 2 surface: opaque-pass greedy meshing only. Transparent water / glass
// / leaves mesh pass lands with the fluid+renderer integration in Phase 6
// (§2.6). This header reserves the pass enum but only `OPAQUE` is legal at
// Phase 2 — calling with any other value returns DEMEN_MESH_ERR_PASS_UNSUPPORTED.
// =============================================================================
#pragma once

#include "demen/abi.hpp"
#include "demen/voxel_store.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Result codes.
// -----------------------------------------------------------------------------
#define DEMEN_MESH_OK                   0
#define DEMEN_MESH_ERR_OOM              1
#define DEMEN_MESH_ERR_CHUNK_NOT_LOADED 2   // §2.3 apron not resident
#define DEMEN_MESH_ERR_BUFFER_SIZE      3   // caller-provided buffer too small
#define DEMEN_MESH_ERR_PASS_UNSUPPORTED 4   // transparent pass pre-Phase 6
#define DEMEN_MESH_ERR_INVALID_HANDLE   5

// -----------------------------------------------------------------------------
// Handles. A mesh is owned by C++, built on a worker thread, and lives until
// the owning chunk unloads or an explicit release call. Zero is always null.
// -----------------------------------------------------------------------------
typedef uint64_t demen_mesh_t;

typedef enum demen_mesh_pass {
    DEMEN_MESH_PASS_OPAQUE      = 0,   // Phase 2: the only legal value
    DEMEN_MESH_PASS_TRANSPARENT = 1,   // reserved for Phase 6
    DEMEN_MESH_PASS_TRANSLUCENT = 2,   // reserved for Phase 6
} demen_mesh_pass;

// -----------------------------------------------------------------------------
// Vertex layout — blittable, 32 bytes, naturally aligned. Locked at Phase 2.
// Bumping size or reordering fields breaks the renderer's bindless pipeline
// layout (Appendix C) and is a Planner decision.
//
// Packing notes (§2.11):
//   - pos is int16_t in voxel-local coordinates (0..32 inclusive for apron
//     seam); fits quad corners exactly, avoids float for determinism.
//   - normal_face is 0..5 (enum demen_mesh_face); the renderer reads it as
//     an index into a 6-entry lookup of normal + tangent.
//   - atlas_slot is the bindless texture-array index, written by the
//     meshing pass directly from the voxel's block_id -> material lookup.
//     Phase 2 uses texture_composition's alphabetical-stem slot mapping.
//   - uv is int16_t in tile-local UV space: [0, tile_size] per axis. The
//     shader divides by tile_size in a constant buffer.
//   - ao is the 4-sample ambient-occlusion weight baked at mesh time
//     (0..255). Cheap to compute once, no runtime cost, eliminates
//     SSAO complexity Layer 1 does not earn (§2.11 rung 1).
// -----------------------------------------------------------------------------
typedef enum demen_mesh_face {
    DEMEN_MESH_FACE_NEG_X = 0,
    DEMEN_MESH_FACE_POS_X = 1,
    DEMEN_MESH_FACE_NEG_Y = 2,
    DEMEN_MESH_FACE_POS_Y = 3,
    DEMEN_MESH_FACE_NEG_Z = 4,
    DEMEN_MESH_FACE_POS_Z = 5,
} demen_mesh_face;

typedef struct demen_mesh_vertex {
    int16_t  pos[3];        // voxel-local [0, 32]
    uint8_t  normal_face;   // demen_mesh_face
    uint8_t  ao;            // 0..255
    int16_t  uv[2];         // tile-local UV, int16 fixed point
    uint32_t atlas_slot;    // bindless texture-array index
    uint32_t _pad;          // reserved; must be 0 (matches 32-byte stride)
} demen_mesh_vertex;

// -----------------------------------------------------------------------------
// Build statistics. Populated by demen_mesh_build; read by B-MESH harness and
// by the renderer for budget diagnostics. Blittable.
// -----------------------------------------------------------------------------
typedef struct demen_mesh_stats {
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t quad_count;        // greedy quads emitted (pre-triangulation)
    uint32_t empty_faces;       // culled by apron-occlusion test
    uint64_t build_nanos;       // wall-clock nanoseconds for this build
} demen_mesh_stats;

// -----------------------------------------------------------------------------
// Build / lifecycle. One mesh object per (chunk, pass). Rebuilding a mesh on
// the same handle reuses the underlying buffer when possible (§2.11 rung 2 —
// batch N mesh uploads into one persistent GPU buffer in Phase 3).
// -----------------------------------------------------------------------------

/// Build (or rebuild) a mesh for one loaded chunk. The chunk handle must have
/// been acquired from the voxel_store (§2.3 apron residency rule). On first
/// call with out_mesh == 0, the subsystem allocates a new mesh handle; on
/// subsequent calls with a live handle, the mesh is rebuilt in place.
///
/// Allocation-free on the hot path: after the first build the internal buffer
/// grows only when vertex_count exceeds the previous high-water mark. The
/// B-ALLOC harness (invariant #1) runs this in a tight loop and asserts zero
/// managed-side allocations.
DEMEN_API int demen_mesh_build(demen_chunk_t  chunk,
                               demen_mesh_pass pass,
                               demen_mesh_t*  out_mesh,
                               demen_mesh_stats* out_stats /* nullable */);

/// Release a mesh handle. Safe on the null handle (returns DEMEN_MESH_OK).
/// Does not unload the owning chunk; the caller's chunk_acquire/release pair
/// is independent of this.
DEMEN_API int demen_mesh_release(demen_mesh_t mesh);

// -----------------------------------------------------------------------------
// Read-back — used by the renderer's GPU upload path and by B-MESH to diff
// mesh output across runs (invariant #2 — determinism).
// -----------------------------------------------------------------------------

/// Copy the mesh's vertex array into caller memory. `buffer_len_verts` must
/// be ≥ vertex_count from the most recent build. Non-destructive; the mesh
/// remains live.
DEMEN_API int demen_mesh_copy_vertices(demen_mesh_t mesh,
                                       demen_mesh_vertex* out_verts,
                                       uint32_t buffer_len_verts);

/// Copy the mesh's index array (u32 indices into the vertex array). Greedy
/// quads are triangulated at build time; the index array is always a pure
/// triangle list for the renderer.
DEMEN_API int demen_mesh_copy_indices(demen_mesh_t mesh,
                                      uint32_t* out_indices,
                                      uint32_t buffer_len_indices);

DEMEN_API int demen_mesh_get_stats(demen_mesh_t mesh,
                                   demen_mesh_stats* out_stats);

// -----------------------------------------------------------------------------
// Whole-world cold pass — what B-MESH times (§E.4 target: <500 ms for the
// 12-chunk-radius default world, reference hardware). The implementation
// parallelises across the available core count (§2.11 rung 6 before rung 7)
// and reuses per-thread scratch buffers to stay allocation-free.
//
// `out_meshes` must point to at least (2*radius_chunks+1)^2 * column_height
// entries; the call fills it in row-major (z, x, y) chunk order and writes
// the populated count to `out_count`. Caller owns the handles thereafter.
// -----------------------------------------------------------------------------

/// Build an opaque mesh for every loaded chunk in the given chunk-coordinate
/// AABB. Returns the first non-OK error encountered; partial success leaves
/// the already-built handles in `out_meshes[0 .. *out_count - 1]`.
DEMEN_API int demen_mesh_build_region(
    demen_world_t   world,
    int32_t         cx_min, int32_t cy_min, int32_t cz_min,
    int32_t         cx_max, int32_t cy_max, int32_t cz_max,
    demen_mesh_pass pass,
    demen_mesh_t*   out_meshes,
    uint32_t        out_meshes_capacity,
    uint32_t*       out_count);

#ifdef __cplusplus
}
#endif
