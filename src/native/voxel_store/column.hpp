// =============================================================================
// column.hpp — chunk column + per-(x,z) ColumnCell tracking.
//
// A "column" is the stack of chunks at world chunk coordinates (cx, cz),
// running from world_min_cy to world_max_cy inclusive. The streaming and
// serialisation unit (§2.3). Maintains a live 32x32 ColumnCell array
// updated on every voxel edit (invariant #6).
// =============================================================================
#pragma once

#include "demen/voxel_store.hpp"
#include "palette_chunk.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace demen::voxel_store {

constexpr uint32_t kColumnMaxChunks = 64;  // Appendix A §A.7.1

// Block-id traits. Phase 1 ships a minimal built-in table; Layer 2 replaces
// this with data-driven material flags (§2.9).
inline bool is_solid(uint16_t block_id) noexcept {
    // 0 = air. Reserved soft-water id = 2. Everything else counts as terrain.
    return block_id != 0 && block_id != 2;
}
inline bool is_water(uint16_t block_id) noexcept {
    return block_id == 2;
}

// -----------------------------------------------------------------------------
// ChunkColumn — owns its chunks and its ColumnCell strip.
// -----------------------------------------------------------------------------
class ChunkColumn {
public:
    ChunkColumn(int32_t cx, int32_t cz, int32_t base_cy, uint32_t n_chunks);

    int32_t  cx()         const noexcept { return cx_; }
    int32_t  cz()         const noexcept { return cz_; }
    int32_t  base_cy()    const noexcept { return base_cy_; }
    uint32_t n_chunks()   const noexcept { return static_cast<uint32_t>(chunks_.size()); }

    // Chunk-local voxel get/set. (lx, ly_abs, lz) has ly given in absolute
    // world voxel Y; the column clamps to its Y range.
    uint16_t get_voxel(uint32_t lx, int32_t y, uint32_t lz) const noexcept;
    bool     set_voxel(uint32_t lx, int32_t y, uint32_t lz, uint16_t block_id);

    // Fill a box in local (x,z) * world-Y voxel coords. Returns true if any
    // voxel actually changed. Updates ColumnCell incrementally.
    bool fill_box(uint32_t lx0, int32_t y0, uint32_t lz0,
                  uint32_t lx1, int32_t y1, uint32_t lz1,
                  uint16_t block_id);

    // Column-cell access (§2.3.1).
    const demen_column_cell& cell(uint32_t lx, uint32_t lz) const noexcept {
        return cells_[cell_index(lx, lz)];
    }
    demen_column_cell* cells_raw() noexcept { return cells_.data(); }
    const demen_column_cell* cells_raw() const noexcept { return cells_.data(); }

    // Whole-column access for region I/O.
    PalettedChunk&       chunk_ref(uint32_t ci)       { return *chunks_[ci]; }
    const PalettedChunk& chunk_ref(uint32_t ci) const { return *chunks_[ci]; }

    // After loading from disk, the caller fills chunks + cells_ and then
    // calls recompute_cells() to validate them. (Uses the on-disk cells
    // directly by default; recompute is only for corruption-recovery or tests.)
    void recompute_cells();

    // For the round-trip comparator.
    size_t memory_bytes() const noexcept;

private:
    static uint32_t cell_index(uint32_t lx, uint32_t lz) noexcept {
        return lz * kChunkEdge + lx;
    }

    // Given a world voxel Y, return (chunk_index_in_column, local_y_within_chunk).
    // Returns {UINT32_MAX, 0} if Y is out of the column's Y range.
    std::pair<uint32_t, uint32_t> y_to_chunk_local(int32_t y) const noexcept;

    // Recompute the column cell at (lx, lz) by scanning its vertical stack
    // top-down. Called when a voxel edit MAY have removed the topmost
    // terrain or water voxel and the next-highest has to be found.
    void rescan_cell(uint32_t lx, uint32_t lz);

    int32_t cx_, cz_;
    int32_t base_cy_;

    std::vector<std::unique_ptr<PalettedChunk>> chunks_;      // size = n_chunks
    std::array<demen_column_cell, DEMEN_COLUMN_CELLS> cells_;
};

} // namespace demen::voxel_store
