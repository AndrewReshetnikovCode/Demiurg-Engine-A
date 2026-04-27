// =============================================================================
// world.hpp — the World. Owns ChunkColumns keyed by (cx, cz), dispatches
// voxel edits, handles acquire/release lifetime for the ABI.
// =============================================================================
#pragma once

#include "column.hpp"
#include "demen/voxel_store.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace demen::voxel_store {

// Key for the column map. Packs two i32 into a u64; std::hash<uint64_t>
// is good enough for Phase 1.
struct ColumnKey {
    int32_t cx;
    int32_t cz;
    bool operator==(const ColumnKey& o) const noexcept { return cx == o.cx && cz == o.cz; }
};
struct ColumnKeyHash {
    size_t operator()(const ColumnKey& k) const noexcept {
        const uint64_t a = static_cast<uint32_t>(k.cx);
        const uint64_t b = static_cast<uint32_t>(k.cz);
        return static_cast<size_t>(a * 0x9E3779B97F4A7C15ULL ^ b);
    }
};

class World {
public:
    World(std::filesystem::path dir, const demen_world_params& params);
    ~World();

    const std::filesystem::path& dir()  const noexcept { return dir_; }
    const demen_world_params&    params() const noexcept { return params_; }

    // Voxel access. Out-of-bounds returns the 0/false case; the ABI wrapper
    // maps that to DEMEN_VS_ERR_OUT_OF_BOUNDS.
    uint16_t get_voxel(int32_t x, int32_t y, int32_t z);
    bool     set_voxel(int32_t x, int32_t y, int32_t z, uint16_t block_id);

    void fill_box(int32_t x0, int32_t y0, int32_t z0,
                  int32_t x1, int32_t y1, int32_t z1,
                  uint16_t block_id);

    // Column metadata point queries (invariant #6).
    bool get_column_cell(int32_t x, int32_t z, demen_column_cell* out) noexcept;

    // Bulk column read. Caller-provided buffer; we write
    // ((cx_max-cx_min+1) * (cz_max-cz_min+1) * 1024) demen_column_cells.
    bool copy_columns_bulk(int32_t cx_min, int32_t cz_min,
                           int32_t cx_max, int32_t cz_max,
                           demen_column_cell* out, size_t buf_cells);

    // Chunk acquire/release (for the ABI's demen_chunk_t handle).
    ChunkColumn*  acquire_column(int32_t cx, int32_t cz);

    // Flush + close. Flush writes all columns to disk; close tears down.
    void flush();

    // Testing: force-materialise a column so subsequent writes don't trigger
    // region I/O.
    ChunkColumn* ensure_column(int32_t cx, int32_t cz);

    // For bounds-check. Returns true if world-voxel (x, y, z) is inside the
    // configured bounds; streaming worlds only check Y bounds.
    bool in_bounds(int32_t x, int32_t y, int32_t z) const noexcept;

    // Enumerate loaded columns (for flush). Not part of the ABI.
    template <typename F>
    void for_each_column(F&& fn) {
        for (auto& [key, col] : columns_) fn(*col);
    }

private:
    std::filesystem::path      dir_;
    demen_world_params         params_{};
    std::unordered_map<ColumnKey, std::unique_ptr<ChunkColumn>, ColumnKeyHash> columns_;

    // How tall is a column? (max_cy - min_cy + 1) chunks, capped at
    // kColumnMaxChunks (Appendix A §A.7.1).
    uint32_t column_height_chunks_ = 0;

    ChunkColumn* load_or_create_column(int32_t cx, int32_t cz);
};

} // namespace demen::voxel_store
