// =============================================================================
// world.cpp
// =============================================================================
#include "world.hpp"
#include "region_io.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <limits>

namespace fs = std::filesystem;

namespace demen::voxel_store {

// -----------------------------------------------------------------------------
// Integer floor-divide for signed values — used to convert world voxel x,z
// to chunk coords when x can be negative. Standard trick so -1 / 32 -> -1
// rather than the default C-rounded-toward-zero 0.
// -----------------------------------------------------------------------------
static int32_t floordiv(int32_t a, int32_t b) {
    int32_t q = a / b;
    int32_t r = a % b;
    if ((r != 0) && ((r < 0) != (b < 0))) --q;
    return q;
}
static uint32_t floormod(int32_t a, int32_t b) {
    int32_t r = a % b;
    if ((r != 0) && ((r < 0) != (b < 0))) r += b;
    return static_cast<uint32_t>(r);
}

// -----------------------------------------------------------------------------
// World ctor / dtor
// -----------------------------------------------------------------------------
World::World(fs::path dir, const demen_world_params& params)
    : dir_(std::move(dir))
    , params_(params)
{
    const int32_t height_chunks = params.max_chunk_y - params.min_chunk_y + 1;
    if (height_chunks <= 0 || static_cast<uint32_t>(height_chunks) > kColumnMaxChunks) {
        // Clamp to legal range. Appendix A §A.7.1.
        column_height_chunks_ = std::min<uint32_t>(
            (height_chunks < 1) ? 1u : static_cast<uint32_t>(height_chunks),
            kColumnMaxChunks);
    } else {
        column_height_chunks_ = static_cast<uint32_t>(height_chunks);
    }
}

World::~World() {
    // Best-effort flush; errors ignored at dtor time.
    try { flush(); } catch (...) {}
}

bool World::in_bounds(int32_t x, int32_t y, int32_t z) const noexcept {
    const int32_t cx = floordiv(x, static_cast<int32_t>(kChunkEdge));
    const int32_t cz = floordiv(z, static_cast<int32_t>(kChunkEdge));
    const int32_t cy = floordiv(y, static_cast<int32_t>(kChunkEdge));
    if (cy < params_.min_chunk_y || cy > params_.max_chunk_y) return false;
    if (params_.scale == DEMEN_WORLD_STREAMING) return true;
    return cx >= params_.bounds_min_chunk_x && cx <= params_.bounds_max_chunk_x
        && cz >= params_.bounds_min_chunk_z && cz <= params_.bounds_max_chunk_z;
}

// -----------------------------------------------------------------------------
// Column materialisation
// -----------------------------------------------------------------------------
ChunkColumn* World::load_or_create_column(int32_t cx, int32_t cz) {
    auto col = std::make_unique<ChunkColumn>(
        cx, cz, params_.min_chunk_y, column_height_chunks_);

    // Phase 1: if the region file exists on disk, load from it. Otherwise
    // the column is "freshly empty" (all air).
    if (fs::exists(dir_)) {
        (void)region_load_column_if_exists(dir_, *col);  // no-op if absent
    }

    ChunkColumn* raw = col.get();
    columns_.emplace(ColumnKey{cx, cz}, std::move(col));
    return raw;
}

ChunkColumn* World::ensure_column(int32_t cx, int32_t cz) {
    auto it = columns_.find(ColumnKey{cx, cz});
    if (it != columns_.end()) return it->second.get();
    return load_or_create_column(cx, cz);
}

ChunkColumn* World::acquire_column(int32_t cx, int32_t cz) {
    return ensure_column(cx, cz);
}

// -----------------------------------------------------------------------------
// Voxel get / set
// -----------------------------------------------------------------------------
uint16_t World::get_voxel(int32_t x, int32_t y, int32_t z) {
    if (!in_bounds(x, y, z)) return 0;
    const int32_t cx = floordiv(x, static_cast<int32_t>(kChunkEdge));
    const int32_t cz = floordiv(z, static_cast<int32_t>(kChunkEdge));
    const uint32_t lx = floormod(x, static_cast<int32_t>(kChunkEdge));
    const uint32_t lz = floormod(z, static_cast<int32_t>(kChunkEdge));
    auto* col = ensure_column(cx, cz);
    return col->get_voxel(lx, y, lz);
}

bool World::set_voxel(int32_t x, int32_t y, int32_t z, uint16_t block_id) {
    if (!in_bounds(x, y, z)) return false;
    const int32_t cx = floordiv(x, static_cast<int32_t>(kChunkEdge));
    const int32_t cz = floordiv(z, static_cast<int32_t>(kChunkEdge));
    const uint32_t lx = floormod(x, static_cast<int32_t>(kChunkEdge));
    const uint32_t lz = floormod(z, static_cast<int32_t>(kChunkEdge));
    return ensure_column(cx, cz)->set_voxel(lx, y, lz, block_id);
}

void World::fill_box(int32_t x0, int32_t y0, int32_t z0,
                     int32_t x1, int32_t y1, int32_t z1,
                     uint16_t block_id)
{
    // Canonicalise coords to half-open low..high.
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);
    if (z1 < z0) std::swap(z0, z1);
    if (x0 == x1 || y0 == y1 || z0 == z1) return;

    // Per-chunk-column dispatch. ChunkColumn::fill_box handles the per-chunk
    // Y slicing and hits PalettedChunk::fill_box's whole-chunk uniform fast
    // path when the caller hands us a chunk-aligned box. §2.11 rung 2 —
    // batch, don't route every voxel through set_voxel.
    const int32_t edge = static_cast<int32_t>(kChunkEdge);
    const int32_t cx_min = floordiv(x0,       edge);
    const int32_t cx_max = floordiv(x1 - 1,   edge);
    const int32_t cz_min = floordiv(z0,       edge);
    const int32_t cz_max = floordiv(z1 - 1,   edge);

    for (int32_t cx = cx_min; cx <= cx_max; ++cx) {
        for (int32_t cz = cz_min; cz <= cz_max; ++cz) {
            if (params_.scale == DEMEN_WORLD_FINITE_BOUNDED) {
                if (cx < params_.bounds_min_chunk_x || cx > params_.bounds_max_chunk_x) continue;
                if (cz < params_.bounds_min_chunk_z || cz > params_.bounds_max_chunk_z) continue;
            }
            auto* col = ensure_column(cx, cz);
            const int32_t chunk_x0 = cx * edge;
            const int32_t chunk_z0 = cz * edge;
            const uint32_t lx0 = static_cast<uint32_t>(std::max(x0 - chunk_x0, 0));
            const uint32_t lx1 = static_cast<uint32_t>(std::min(x1 - chunk_x0, edge));
            const uint32_t lz0 = static_cast<uint32_t>(std::max(z0 - chunk_z0, 0));
            const uint32_t lz1 = static_cast<uint32_t>(std::min(z1 - chunk_z0, edge));
            // Clamp Y to the column's vertical span.
            const int32_t ymin = params_.min_chunk_y * edge;
            const int32_t ymax = (params_.max_chunk_y + 1) * edge;
            const int32_t cy0 = std::max(y0, ymin);
            const int32_t cy1 = std::min(y1, ymax);
            if (cy0 >= cy1) continue;
            col->fill_box(lx0, cy0, lz0, lx1, cy1, lz1, block_id);
        }
    }
}

// -----------------------------------------------------------------------------
// Column-metadata queries (invariant #6)
// -----------------------------------------------------------------------------
bool World::get_column_cell(int32_t x, int32_t z, demen_column_cell* out) noexcept {
    if (!out) return false;
    // Use a huge Y so the bounds-check only tests x,z ranges... easier to
    // compute the chunk coords and bounds-check those directly:
    const int32_t cx = floordiv(x, static_cast<int32_t>(kChunkEdge));
    const int32_t cz = floordiv(z, static_cast<int32_t>(kChunkEdge));
    if (params_.scale == DEMEN_WORLD_FINITE_BOUNDED) {
        if (cx < params_.bounds_min_chunk_x || cx > params_.bounds_max_chunk_x) return false;
        if (cz < params_.bounds_min_chunk_z || cz > params_.bounds_max_chunk_z) return false;
    }
    const uint32_t lx = floormod(x, static_cast<int32_t>(kChunkEdge));
    const uint32_t lz = floormod(z, static_cast<int32_t>(kChunkEdge));
    *out = ensure_column(cx, cz)->cell(lx, lz);
    return true;
}

bool World::copy_columns_bulk(int32_t cx_min, int32_t cz_min,
                              int32_t cx_max, int32_t cz_max,
                              demen_column_cell* out, size_t buf_cells)
{
    if (!out) return false;
    if (cx_max < cx_min || cz_max < cz_min) return false;
    const size_t n_cols =
        static_cast<size_t>(cx_max - cx_min + 1) *
        static_cast<size_t>(cz_max - cz_min + 1);
    const size_t need = n_cols * DEMEN_COLUMN_CELLS;
    if (buf_cells < need) return false;

    size_t o = 0;
    for (int32_t cz = cz_min; cz <= cz_max; ++cz) {
        for (int32_t cx = cx_min; cx <= cx_max; ++cx) {
            auto* col = ensure_column(cx, cz);
            std::memcpy(&out[o], col->cells_raw(),
                        DEMEN_COLUMN_CELLS * sizeof(demen_column_cell));
            o += DEMEN_COLUMN_CELLS;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// Flush
// -----------------------------------------------------------------------------
void World::flush() {
    if (!fs::exists(dir_)) fs::create_directories(dir_);
    region_write_all_dirty_columns(dir_, *this);
}

} // namespace demen::voxel_store
