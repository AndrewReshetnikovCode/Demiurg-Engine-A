// =============================================================================
// meshing_abi.cpp — C ABI for the meshing subsystem (§meshing.hpp).
// =============================================================================
#include "demen/meshing.hpp"

#include "chunk_view.hpp"
#include "greedy_mesher.hpp"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

using namespace demen::meshing;

namespace {

struct MeshRecord {
    demen_chunk_t   chunk;
    demen_mesh_pass pass;
    MeshBuffer      buf;
};

std::mutex g_mu;
std::unordered_map<uint64_t, std::unique_ptr<MeshRecord>> g_meshes;
std::atomic<uint64_t> g_next_id{1};

// Separate registry for the "chunk handle -> (world, cx, cy, cz)" mapping is
// owned by voxel_store; meshing reads the chunk only via the public ABI.
// We keep a minimal shadow so mesh rebuild can ask voxel_store for the
// voxels of the chunk handle we were given.
//
// voxel_store's ABI currently gives us a chunk handle that binds to a
// (world, cx, cy, cz). Meshing needs those coordinates to fill_chunk_view.
// Per the interface, the helper demen_chunk_copy_voxels already exists —
// we use it to populate the 32^3 interior, then scan the apron via
// demen_world_get_voxel. That keeps meshing purely downstream of the public
// voxel_store surface.
//
// The stored world+coords live in a side-channel registry populated lazily
// from the first build call; voxel_store stores them already and we ask it
// via the (future) helper demen_chunk_info. To keep Phase 2 self-contained
// without changing voxel_store's public surface, we route the apron read
// through demen_world_get_voxel exclusively — that means we also need to
// know which world the chunk belongs to. We therefore require the caller
// to have acquired the chunk through demen_chunk_acquire and for the
// mesh_build to accept an additional implicit mapping: we store the
// (world, cx, cy, cz) alongside the mesh record when build is first
// called with a world handle that exposes them via a lookup helper we
// add below.
//
// Pragmatic route for Phase 2: we reconstruct the chunk's coordinates by
// scanning the whole 32^3 slab via demen_chunk_copy_voxels for the interior
// and disable apron reads entirely — aprons become air. This matches the
// "simplest implementation that clears the gate" directive (§2.11): visible
// border quads across chunks will appear, but the renderer can still draw
// the world. Full apron meshing lands when voxel_store exposes a chunk-
// coordinates helper (proposed delta queued for the Planner).
//
// See the TODO note in chunk_view.cpp's fill_from_interior() path below.

struct ChunkCoord {
    demen_world_t world;
    int32_t       cx, cy, cz;
};

// Mesh-side shadow table. Not populated in this Phase-2 landing; see note.
std::unordered_map<demen_chunk_t, ChunkCoord> g_chunk_coords;

void fill_view_from_interior_only(demen_chunk_t chunk, ChunkView& view) {
    uint16_t buf[DEMEN_CHUNK_VOXELS];
    std::memset(buf, 0, sizeof(buf));
    (void)demen_chunk_copy_voxels(chunk, buf, DEMEN_CHUNK_VOXELS);
    // chunk_copy_voxels layout: x fastest, then y, then z (voxel_store-internal).
    for (int z = 0; z < kEdge; ++z)
        for (int y = 0; y < kEdge; ++y)
            for (int x = 0; x < kEdge; ++x)
                view.set(x, y, z, buf[(z * kEdge + y) * kEdge + x]);
    // Apron left as air (0). Border quads across chunks will render twice,
    // but every quad remains geometrically valid. Tracked in the Planner's
    // Phase-2 "proposed deltas" for a voxel_store helper that exposes chunk
    // coords given a chunk handle.
}

// Phase 2: no block->slot lookup plumbed here yet. The renderer, in Phase 3,
// will register a callback via demen_mesh_set_slot_callback (not yet in the
// public header — proposed delta). Until then every face gets slot 0, which
// the atlas populates with the first alphabetical material. That yields a
// visually identifiable but uniformly-textured world — enough to ship the
// opaque pass.
uint32_t default_slot_cb(uint16_t /*block_id*/, void* /*user*/) {
    return 0;
}

} // namespace

extern "C" {

DEMEN_API int demen_mesh_build(demen_chunk_t chunk,
                               demen_mesh_pass pass,
                               demen_mesh_t* out_mesh,
                               demen_mesh_stats* out_stats) {
    if (chunk == 0 || !out_mesh) return DEMEN_MESH_ERR_INVALID_HANDLE;
    if (pass != DEMEN_MESH_PASS_OPAQUE) return DEMEN_MESH_ERR_PASS_UNSUPPORTED;

    ChunkView view;
    fill_view_from_interior_only(chunk, view);

    // Acquire or create the mesh record.
    MeshRecord* rec = nullptr;
    uint64_t id = *out_mesh;
    {
        std::lock_guard lk(g_mu);
        if (id != 0) {
            auto it = g_meshes.find(id);
            if (it == g_meshes.end()) return DEMEN_MESH_ERR_INVALID_HANDLE;
            rec = it->second.get();
        } else {
            auto r = std::make_unique<MeshRecord>();
            r->chunk = chunk;
            r->pass  = pass;
            id = g_next_id.fetch_add(1, std::memory_order_relaxed);
            rec = r.get();
            g_meshes.emplace(id, std::move(r));
        }
    }

    build_opaque_mesh(view, rec->buf, default_slot_cb, nullptr);
    *out_mesh = id;
    if (out_stats) *out_stats = rec->buf.stats;
    return DEMEN_MESH_OK;
}

DEMEN_API int demen_mesh_release(demen_mesh_t mesh) {
    if (mesh == 0) return DEMEN_MESH_OK;
    std::lock_guard lk(g_mu);
    g_meshes.erase(mesh);
    return DEMEN_MESH_OK;
}

DEMEN_API int demen_mesh_copy_vertices(demen_mesh_t mesh,
                                       demen_mesh_vertex* out, uint32_t len) {
    if (!out) return DEMEN_MESH_ERR_INVALID_HANDLE;
    std::lock_guard lk(g_mu);
    auto it = g_meshes.find(mesh);
    if (it == g_meshes.end()) return DEMEN_MESH_ERR_INVALID_HANDLE;
    const auto& v = it->second->buf.verts;
    if (len < v.size()) return DEMEN_MESH_ERR_BUFFER_SIZE;
    std::memcpy(out, v.data(), v.size() * sizeof(demen_mesh_vertex));
    return DEMEN_MESH_OK;
}

DEMEN_API int demen_mesh_copy_indices(demen_mesh_t mesh,
                                      uint32_t* out, uint32_t len) {
    if (!out) return DEMEN_MESH_ERR_INVALID_HANDLE;
    std::lock_guard lk(g_mu);
    auto it = g_meshes.find(mesh);
    if (it == g_meshes.end()) return DEMEN_MESH_ERR_INVALID_HANDLE;
    const auto& i = it->second->buf.idx;
    if (len < i.size()) return DEMEN_MESH_ERR_BUFFER_SIZE;
    std::memcpy(out, i.data(), i.size() * sizeof(uint32_t));
    return DEMEN_MESH_OK;
}

DEMEN_API int demen_mesh_get_stats(demen_mesh_t mesh, demen_mesh_stats* out) {
    if (!out) return DEMEN_MESH_ERR_INVALID_HANDLE;
    std::lock_guard lk(g_mu);
    auto it = g_meshes.find(mesh);
    if (it == g_meshes.end()) return DEMEN_MESH_ERR_INVALID_HANDLE;
    *out = it->second->buf.stats;
    return DEMEN_MESH_OK;
}

DEMEN_API int demen_mesh_build_region(
    demen_world_t   world,
    int32_t cx_min, int32_t cy_min, int32_t cz_min,
    int32_t cx_max, int32_t cy_max, int32_t cz_max,
    demen_mesh_pass pass,
    demen_mesh_t*   out_meshes,
    uint32_t        out_capacity,
    uint32_t*       out_count) {
    if (pass != DEMEN_MESH_PASS_OPAQUE) return DEMEN_MESH_ERR_PASS_UNSUPPORTED;
    if (!out_meshes || !out_count)      return DEMEN_MESH_ERR_INVALID_HANDLE;
    *out_count = 0;

    for (int32_t cz = cz_min; cz <= cz_max; ++cz) {
        for (int32_t cx = cx_min; cx <= cx_max; ++cx) {
            for (int32_t cy = cy_min; cy <= cy_max; ++cy) {
                if (*out_count >= out_capacity) return DEMEN_MESH_ERR_BUFFER_SIZE;

                demen_chunk_t chunk = 0;
                const int acq = demen_chunk_acquire(world, cx, cy, cz, &chunk);
                if (acq != DEMEN_VS_OK) return DEMEN_MESH_ERR_CHUNK_NOT_LOADED;

                demen_mesh_t handle = 0;
                const int rc = demen_mesh_build(chunk, pass, &handle, nullptr);
                // Release the chunk acquire — the meshing path took its copy.
                demen_chunk_release(chunk);

                if (rc != DEMEN_MESH_OK) return rc;
                out_meshes[(*out_count)++] = handle;
            }
        }
    }
    return DEMEN_MESH_OK;
}

} // extern "C"
