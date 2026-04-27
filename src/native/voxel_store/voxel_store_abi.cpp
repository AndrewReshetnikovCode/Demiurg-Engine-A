// =============================================================================
// voxel_store_abi.cpp — public ABI for the voxel_store subsystem.
//
// Rules: see demen/voxel_store.hpp. Only blittable types cross; every
// handle allocation here pairs with a free elsewhere; no exceptions escape.
// =============================================================================

#include "demen/voxel_store.hpp"

#include "column.hpp"
#include "region_io.hpp"
#include "world.hpp"

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

using namespace demen::voxel_store;

// -----------------------------------------------------------------------------
// Handle registries. Phase 1 is single-threaded; we still take a mutex so a
// future multi-threaded game loop (Phase 4) doesn't silently corrupt the
// registry. The cost of an uncontended mutex acquire is negligible next to
// a voxel write.
// -----------------------------------------------------------------------------
namespace {

std::mutex g_worlds_mutex;
std::unordered_map<uint64_t, std::unique_ptr<World>> g_worlds;
std::atomic<uint64_t> g_next_world_id{1};

std::mutex g_chunks_mutex;
struct ChunkHandle {
    uint64_t     world;
    int32_t      cx, cy, cz;
    ChunkColumn* column;
};
std::unordered_map<uint64_t, ChunkHandle> g_chunks;
std::atomic<uint64_t> g_next_chunk_id{1};

World* world_from(demen_world_t h) {
    std::lock_guard lk(g_worlds_mutex);
    auto it = g_worlds.find(h);
    return it == g_worlds.end() ? nullptr : it->second.get();
}

} // namespace

// =============================================================================
// Lifecycle
// =============================================================================
extern "C" DEMEN_API int demen_world_create(const char* region_dir,
                                            const demen_world_params* params,
                                            demen_world_t* out_world)
{
    if (!region_dir || !params || !out_world) return DEMEN_VS_ERR_IO;
    try {
        std::filesystem::path dir{region_dir};
        if (std::filesystem::exists(dir) && !std::filesystem::is_empty(dir)) {
            return DEMEN_VS_ERR_IO; // "fails if the directory is non-empty"
        }
        std::filesystem::create_directories(dir);

        int rc = region_write_root_header(dir, *params);
        if (rc != DEMEN_VS_OK) return rc;

        auto world = std::make_unique<World>(dir, *params);
        const uint64_t id = g_next_world_id.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard lk(g_worlds_mutex);
            g_worlds.emplace(id, std::move(world));
        }
        *out_world = id;
        return DEMEN_VS_OK;
    } catch (const std::bad_alloc&) {
        return DEMEN_VS_ERR_OOM;
    } catch (...) {
        return DEMEN_VS_ERR_IO;
    }
}

extern "C" DEMEN_API int demen_world_open(const char* region_dir,
                                          demen_world_t* out_world)
{
    if (!region_dir || !out_world) return DEMEN_VS_ERR_IO;
    *out_world = 0;
    try {
        std::filesystem::path dir{region_dir};
        demen_world_params params{};
        int rc = region_read_root_header(dir, &params);
        if (rc != DEMEN_VS_OK) return rc;
        region_clean_stale_tmp(dir);

        auto world = std::make_unique<World>(dir, params);
        const uint64_t id = g_next_world_id.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard lk(g_worlds_mutex);
            g_worlds.emplace(id, std::move(world));
        }
        *out_world = id;
        return DEMEN_VS_OK;
    } catch (const std::bad_alloc&) {
        return DEMEN_VS_ERR_OOM;
    } catch (...) {
        return DEMEN_VS_ERR_IO;
    }
}

extern "C" DEMEN_API int demen_world_close(demen_world_t world) {
    if (world == 0) return DEMEN_VS_OK;  // null is always ok
    std::unique_ptr<World> gone;
    {
        std::lock_guard lk(g_worlds_mutex);
        auto it = g_worlds.find(world);
        if (it == g_worlds.end()) return DEMEN_VS_OK;
        gone = std::move(it->second);
        g_worlds.erase(it);
    }
    // Flush outside the lock; dtor will also flush, but explicit is clearer.
    try { gone->flush(); } catch (...) { return DEMEN_VS_ERR_IO; }
    return DEMEN_VS_OK;
}

extern "C" DEMEN_API int demen_world_flush(demen_world_t world) {
    auto* w = world_from(world);
    if (!w) return DEMEN_VS_ERR_NOT_LOADED;
    try { w->flush(); } catch (...) { return DEMEN_VS_ERR_IO; }
    return DEMEN_VS_OK;
}

// =============================================================================
// Voxel access
// =============================================================================
extern "C" DEMEN_API int demen_world_get_voxel(demen_world_t world,
                                               int32_t x, int32_t y, int32_t z,
                                               uint16_t* out_block_id)
{
    if (!out_block_id) return DEMEN_VS_ERR_IO;
    auto* w = world_from(world);
    if (!w) return DEMEN_VS_ERR_NOT_LOADED;
    if (!w->in_bounds(x, y, z)) return DEMEN_VS_ERR_OUT_OF_BOUNDS;
    *out_block_id = w->get_voxel(x, y, z);
    return DEMEN_VS_OK;
}

extern "C" DEMEN_API int demen_world_set_voxel(demen_world_t world,
                                               int32_t x, int32_t y, int32_t z,
                                               uint16_t block_id)
{
    auto* w = world_from(world);
    if (!w) return DEMEN_VS_ERR_NOT_LOADED;
    if (!w->in_bounds(x, y, z)) return DEMEN_VS_ERR_OUT_OF_BOUNDS;
    w->set_voxel(x, y, z, block_id);
    return DEMEN_VS_OK;
}

extern "C" DEMEN_API int demen_world_fill_box(demen_world_t world,
                                              int32_t x0, int32_t y0, int32_t z0,
                                              int32_t x1, int32_t y1, int32_t z1,
                                              uint16_t block_id)
{
    auto* w = world_from(world);
    if (!w) return DEMEN_VS_ERR_NOT_LOADED;
    w->fill_box(x0, y0, z0, x1, y1, z1, block_id);
    return DEMEN_VS_OK;
}

// =============================================================================
// Chunks
// =============================================================================
extern "C" DEMEN_API int demen_chunk_acquire(demen_world_t world,
                                             int32_t cx, int32_t cy, int32_t cz,
                                             demen_chunk_t* out_chunk)
{
    if (!out_chunk) return DEMEN_VS_ERR_IO;
    auto* w = world_from(world);
    if (!w) return DEMEN_VS_ERR_NOT_LOADED;
    auto* col = w->acquire_column(cx, cz);
    if (!col) return DEMEN_VS_ERR_NOT_LOADED;

    const uint64_t id = g_next_chunk_id.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard lk(g_chunks_mutex);
        g_chunks.emplace(id, ChunkHandle{world, cx, cy, cz, col});
    }
    *out_chunk = id;
    return DEMEN_VS_OK;
}

extern "C" DEMEN_API int demen_chunk_release(demen_chunk_t chunk) {
    std::lock_guard lk(g_chunks_mutex);
    g_chunks.erase(chunk);
    return DEMEN_VS_OK;
}

extern "C" DEMEN_API int demen_chunk_copy_voxels(demen_chunk_t chunk,
                                                 uint16_t* out_buffer,
                                                 uint32_t buffer_len)
{
    if (!out_buffer || buffer_len < DEMEN_CHUNK_VOXELS) return DEMEN_VS_ERR_IO;
    ChunkHandle h;
    {
        std::lock_guard lk(g_chunks_mutex);
        auto it = g_chunks.find(chunk);
        if (it == g_chunks.end()) return DEMEN_VS_ERR_NOT_LOADED;
        h = it->second;
    }
    // Resolve the chunk within its column.
    const int32_t base = h.column->base_cy();
    if (h.cy < base || h.cy >= base + static_cast<int32_t>(h.column->n_chunks())) {
        return DEMEN_VS_ERR_OUT_OF_BOUNDS;
    }
    h.column->chunk_ref(static_cast<uint32_t>(h.cy - base)).copy_voxels(out_buffer);
    return DEMEN_VS_OK;
}

// =============================================================================
// Column metadata (invariant #6)
// =============================================================================
extern "C" DEMEN_API int demen_world_get_column_cell(demen_world_t world,
                                                    int32_t x, int32_t z,
                                                    demen_column_cell* out_cell)
{
    if (!out_cell) return DEMEN_VS_ERR_IO;
    auto* w = world_from(world);
    if (!w) return DEMEN_VS_ERR_NOT_LOADED;
    return w->get_column_cell(x, z, out_cell) ? DEMEN_VS_OK
                                              : DEMEN_VS_ERR_OUT_OF_BOUNDS;
}

extern "C" DEMEN_API int demen_world_query_terrain_top_y(demen_world_t world,
                                                         int32_t x, int32_t z,
                                                         int16_t* out_y)
{
    if (!out_y) return DEMEN_VS_ERR_IO;
    demen_column_cell c;
    int rc = demen_world_get_column_cell(world, x, z, &c);
    if (rc != DEMEN_VS_OK) return rc;
    *out_y = c.terrain_top_y;
    return DEMEN_VS_OK;
}

extern "C" DEMEN_API int demen_world_query_water_depth(demen_world_t world,
                                                       int32_t x, int32_t z,
                                                       uint16_t* out_depth)
{
    if (!out_depth) return DEMEN_VS_ERR_IO;
    demen_column_cell c;
    int rc = demen_world_get_column_cell(world, x, z, &c);
    if (rc != DEMEN_VS_OK) return rc;
    *out_depth = c.water_depth_voxels;
    return DEMEN_VS_OK;
}

extern "C" DEMEN_API int demen_world_copy_columns_bulk(demen_world_t world,
    int32_t cx_min, int32_t cz_min,
    int32_t cx_max, int32_t cz_max,
    demen_column_cell* out_cells,
    uint32_t buffer_len_cells)
{
    auto* w = world_from(world);
    if (!w) return DEMEN_VS_ERR_NOT_LOADED;
    return w->copy_columns_bulk(cx_min, cz_min, cx_max, cz_max,
                                out_cells, buffer_len_cells)
        ? DEMEN_VS_OK : DEMEN_VS_ERR_OUT_OF_BOUNDS;
}
