// =============================================================================
// column.cpp
// =============================================================================
#include "column.hpp"

#include <cassert>
#include <cstring>
#include <limits>

namespace demen::voxel_store {

ChunkColumn::ChunkColumn(int32_t cx, int32_t cz, int32_t base_cy, uint32_t n_chunks)
    : cx_(cx), cz_(cz), base_cy_(base_cy)
{
    assert(n_chunks > 0 && n_chunks <= kColumnMaxChunks);
    chunks_.reserve(n_chunks);
    for (uint32_t i = 0; i < n_chunks; ++i) {
        chunks_.emplace_back(std::make_unique<PalettedChunk>());
    }
    // Start with empty cells — everything INT16_MIN-y, no water.
    demen_column_cell empty{};
    empty.terrain_top_y   = INT16_MIN;
    empty.water_surface_y = INT16_MIN;
    empty.water_depth_voxels = 0;
    empty.flags = 0;
    cells_.fill(empty);
}

std::pair<uint32_t, uint32_t> ChunkColumn::y_to_chunk_local(int32_t y) const noexcept {
    const int32_t min_y = base_cy_ * static_cast<int32_t>(kChunkEdge);
    const int32_t max_y = (base_cy_ + static_cast<int32_t>(chunks_.size())) * static_cast<int32_t>(kChunkEdge);
    if (y < min_y || y >= max_y) {
        return {UINT32_MAX, 0};
    }
    const int32_t rel = y - min_y;
    const uint32_t ci = static_cast<uint32_t>(rel / static_cast<int32_t>(kChunkEdge));
    const uint32_t ly = static_cast<uint32_t>(rel % static_cast<int32_t>(kChunkEdge));
    return {ci, ly};
}

uint16_t ChunkColumn::get_voxel(uint32_t lx, int32_t y, uint32_t lz) const noexcept {
    const auto [ci, ly] = y_to_chunk_local(y);
    if (ci == UINT32_MAX) return 0;
    return chunks_[ci]->get(lx, ly, lz);
}

// -----------------------------------------------------------------------------
// rescan_cell — full vertical walk to determine the new top voxel values.
// Only called when an edit MAY have invalidated the incremental update.
// Cost: O(column_height_voxels) = up to 64 * 32 = 2048 reads worst case.
// -----------------------------------------------------------------------------
void ChunkColumn::rescan_cell(uint32_t lx, uint32_t lz) {
    int16_t terrain_top = INT16_MIN;
    int16_t water_top   = INT16_MIN;
    uint16_t water_depth = 0;

    // Walk top-down, chunk by chunk.
    for (uint32_t ci_rev = 0; ci_rev < chunks_.size(); ++ci_rev) {
        const uint32_t ci = static_cast<uint32_t>(chunks_.size()) - 1 - ci_rev;
        auto& chk = *chunks_[ci];
        for (uint32_t ly_rev = 0; ly_rev < kChunkEdge; ++ly_rev) {
            const uint32_t ly = kChunkEdge - 1 - ly_rev;
            const uint16_t b = chk.get(lx, ly, lz);
            const int32_t  world_y =
                base_cy_ * static_cast<int32_t>(kChunkEdge)
                + static_cast<int32_t>(ci) * static_cast<int32_t>(kChunkEdge)
                + static_cast<int32_t>(ly);
            if (is_solid(b)) {
                if (terrain_top == INT16_MIN) {
                    terrain_top = static_cast<int16_t>(world_y);
                }
            } else if (is_water(b)) {
                if (water_top == INT16_MIN) {
                    water_top = static_cast<int16_t>(world_y);
                }
                // Only count water above the terrain top (bulk water depth).
                if (terrain_top == INT16_MIN) ++water_depth;
            } else {
                // Air: resets the contiguous-water run when walking below the
                // surface, but here we're walking top-down so that's moot.
            }
            if (terrain_top != INT16_MIN && water_top != INT16_MIN) {
                // We have both tops; keep walking only until we leave water
                // to finish water_depth.
                if (is_solid(b)) goto done;
            }
        }
    }
done:
    auto& c = cells_[cell_index(lx, lz)];
    c.terrain_top_y       = terrain_top;
    c.water_surface_y     = water_top;
    c.water_depth_voxels  = water_depth;
    c.flags = static_cast<uint16_t>(
        c.flags | DEMEN_COLUMN_FLAG_WAVE_RESEED | DEMEN_COLUMN_FLAG_MESH_REBUILD);
}

// -----------------------------------------------------------------------------
// set_voxel — ColumnCell update strategy:
//
//  1. If adding solid at Y above current terrain_top_y -> just raise the top.
//  2. If adding solid at Y below terrain_top_y -> top unchanged.
//  3. If adding water and water_surface_y == INT16_MIN or below Y -> raise.
//  4. If removing (block_id == 0 / non-solid, non-water) AT terrain_top_y
//     or water_surface_y -> invalidated; rescan.
//
// (Case 4 is where the invariant #6 test hits. Rescan is O(column height);
//  per §2.11 that's fine — it's called at most once per edit, on edits that
//  touch the top voxel, which is rare.)
// -----------------------------------------------------------------------------
bool ChunkColumn::set_voxel(uint32_t lx, int32_t y, uint32_t lz, uint16_t block_id) {
    const auto [ci, ly] = y_to_chunk_local(y);
    if (ci == UINT32_MAX) return false;

    const uint16_t prev = chunks_[ci]->get(lx, ly, lz);
    if (prev == block_id) return false;

    chunks_[ci]->set(lx, ly, lz, block_id);

    auto& cell = cells_[cell_index(lx, lz)];
    const int16_t y16 = static_cast<int16_t>(y);
    const bool was_solid = is_solid(prev);
    const bool now_solid = is_solid(block_id);
    const bool was_water = is_water(prev);
    const bool now_water = is_water(block_id);

    bool need_rescan = false;

    if (now_solid) {
        if (cell.terrain_top_y == INT16_MIN || y16 > cell.terrain_top_y) {
            cell.terrain_top_y = y16;
        }
    } else if (was_solid && y16 == cell.terrain_top_y) {
        need_rescan = true;
    }

    if (now_water) {
        if (cell.water_surface_y == INT16_MIN || y16 > cell.water_surface_y) {
            cell.water_surface_y = y16;
        }
        // Conservative: rescan to refresh water_depth, which depends on
        // the contiguous water run above terrain. §2.11 rung 1 says skip
        // cheap speculative specialisation; this is fine.
        need_rescan = true;
    } else if (was_water && y16 == cell.water_surface_y) {
        need_rescan = true;
    }

    if (need_rescan) {
        rescan_cell(lx, lz);
    } else {
        cell.flags = static_cast<uint16_t>(cell.flags | DEMEN_COLUMN_FLAG_MESH_REBUILD);
    }
    return true;
}

// -----------------------------------------------------------------------------
// fill_box — dispatches per-chunk along Y, so the per-chunk whole-chunk fast
// path in PalettedChunk::fill_box fires when the caller hands us a chunk-
// aligned box (§2.11 rung 2, batch). Then scans affected column cells once.
// -----------------------------------------------------------------------------
bool ChunkColumn::fill_box(uint32_t lx0, int32_t y0, uint32_t lz0,
                           uint32_t lx1, int32_t y1, uint32_t lz1,
                           uint16_t block_id)
{
    if (lx0 >= lx1 || lz0 >= lz1 || y0 >= y1) return false;
    bool changed = false;
    const int32_t edge = static_cast<int32_t>(kChunkEdge);

    for (uint32_t ci = 0; ci < chunks_.size(); ++ci) {
        const int32_t chunk_y0 = (base_cy_ + static_cast<int32_t>(ci)) * edge;
        const int32_t chunk_y1 = chunk_y0 + edge;
        if (y1 <= chunk_y0 || y0 >= chunk_y1) continue;
        const uint32_t ly0 = static_cast<uint32_t>(
            (y0 > chunk_y0 ? y0 - chunk_y0 : 0));
        const uint32_t ly1 = static_cast<uint32_t>(
            (y1 < chunk_y1 ? y1 - chunk_y0 : edge));
        if (chunks_[ci]->fill_box(lx0, ly0, lz0, lx1, ly1, lz1, block_id)) {
            changed = true;
        }
    }

    if (changed) {
        // Rescan every affected column cell once. Per §2.11 rung 1 we do
        // this in bulk here rather than incrementally per voxel inside
        // the chunk's fill_box, which would be ~1000x slower.
        for (uint32_t lz = lz0; lz < lz1; ++lz) {
            for (uint32_t lx = lx0; lx < lx1; ++lx) {
                rescan_cell(lx, lz);
            }
        }
    }
    return changed;
}

void ChunkColumn::recompute_cells() {
    for (uint32_t lz = 0; lz < kChunkEdge; ++lz) {
        for (uint32_t lx = 0; lx < kChunkEdge; ++lx) {
            rescan_cell(lx, lz);
        }
    }
}

size_t ChunkColumn::memory_bytes() const noexcept {
    size_t total = sizeof(*this);
    for (const auto& c : chunks_) total += c->memory_bytes();
    return total;
}

} // namespace demen::voxel_store
