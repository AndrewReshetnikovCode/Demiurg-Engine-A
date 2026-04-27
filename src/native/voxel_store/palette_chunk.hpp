// =============================================================================
// palette_chunk.hpp — single 32^3 chunk with palette compression (§2.3).
//
// Internal header; not exposed to C# (only to other TUs of voxel_store).
// Everything here is single-threaded and allocation-aware: a chunk only
// allocates its index buffer lazily when it becomes non-uniform.
// =============================================================================
#pragma once

#include "demen/voxel_store.hpp"

#include <cstdint>
#include <vector>

namespace demen::voxel_store {

constexpr uint32_t kChunkEdge   = DEMEN_CHUNK_EDGE;   // 32
constexpr uint32_t kChunkVoxels = DEMEN_CHUNK_VOXELS; // 32768

// Indices are packed little-endian-in-word. Voxel-to-index mapping is
// (y * 32 + z) * 32 + x, chosen to keep (x,z) strides contiguous for the
// meshing + column-cell passes that traverse by Y last.
constexpr uint32_t index_of(uint32_t x, uint32_t y, uint32_t z) {
    return (y * kChunkEdge + z) * kChunkEdge + x;
}

// -----------------------------------------------------------------------------
// PalettedChunk — owning container. Copyable, not thread-safe.
// -----------------------------------------------------------------------------
class PalettedChunk {
public:
    PalettedChunk();

    // Every chunk starts uniform-air. No allocation until first non-air write.
    uint16_t get(uint32_t x, uint32_t y, uint32_t z) const noexcept;

    // Returns true if the write actually changed the voxel.
    bool set(uint32_t x, uint32_t y, uint32_t z, uint16_t block_id);

    // Fill the whole chunk with a single block_id. Fast path: drops any
    // existing index buffer and becomes uniform again.
    void fill_uniform(uint16_t block_id);

    // Fill a half-open [x0,x1)*[y0,y1)*[z0,z1) sub-box. Returns true if any
    // voxel actually changed.
    bool fill_box(uint32_t x0, uint32_t y0, uint32_t z0,
                  uint32_t x1, uint32_t y1, uint32_t z1,
                  uint16_t block_id);

    // Copy all 32768 voxels into `out` (caller-provided, must hold
    // kChunkVoxels uint16 entries). Used by the chunk-copy ABI and by
    // the round-trip test's comparator.
    void copy_voxels(uint16_t* out) const noexcept;

    // Bytes currently held by this chunk (palette + indices). For memory
    // accounting; sum across chunks must fit the RAM budget (§1.2).
    size_t memory_bytes() const noexcept;

    bool is_uniform() const noexcept { return bits_ == 0; }
    uint8_t  bits_per_index() const noexcept { return bits_; }
    const std::vector<uint16_t>& palette() const noexcept { return palette_; }
    const std::vector<uint8_t>&  indices() const noexcept { return indices_; }

    // Serialisation helpers — used by region_io. Deterministic: palette
    // entries are written in insertion order (invariant #2).
    size_t serialized_size() const noexcept;
    void   serialize_into(uint8_t* out) const noexcept;
    bool   deserialize_from(const uint8_t* in, size_t size);

private:
    // Internal helpers for bit-packed index access.
    uint16_t read_index(uint32_t i) const noexcept;
    void     write_index(uint32_t i, uint16_t idx) noexcept;

    // Promote storage to a wider bits-per-index layout (expanding indices_).
    void grow_bits(uint8_t new_bits);

    // Find an existing palette entry for `block_id`, or insert + return it.
    // May promote the chunk from uniform to bits=1 and grow_bits() further.
    uint16_t intern_palette(uint16_t block_id);

    // Storage state:
    //   bits_ == 0 -> uniform; palette_[0] is the sole value; indices_ empty.
    //   bits_  > 0 -> packed indices_ with palette_ entries.
    uint8_t                bits_ = 0;
    std::vector<uint16_t>  palette_;     // insertion order; palette_[0] starts as 0 (air)
    std::vector<uint8_t>   indices_;     // bit-packed
};

} // namespace demen::voxel_store
