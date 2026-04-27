// =============================================================================
// palette_chunk.cpp — paletted-chunk implementation.
// =============================================================================
#include "palette_chunk.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace demen::voxel_store {

namespace {

// Smallest bits-per-index that fits `n_entries`. Returns 0 if n_entries <= 1
// (i.e. the chunk can stay uniform).
uint8_t bits_for_palette(size_t n_entries) {
    if (n_entries <= 1) return 0;
    if (n_entries <= 2) return 1;
    if (n_entries <= 4) return 2;
    if (n_entries <= 16) return 4;
    if (n_entries <= 256) return 8;
    return 16;
}

size_t indices_bytes(uint8_t bits) {
    // bits_for_palette returns 0/1/2/4/8/16 only.
    return (static_cast<size_t>(kChunkVoxels) * bits) / 8;
}

} // namespace

PalettedChunk::PalettedChunk() {
    palette_.reserve(2);
    palette_.push_back(0); // air = palette[0] always
    // bits_ = 0, indices_ empty -> uniform air.
}

// -----------------------------------------------------------------------------
// Bit-packed reads / writes. Deliberately straightforward — §2.11 rung 7
// (SIMD) has not been justified by a benchmark yet; invariant #8 applies.
// -----------------------------------------------------------------------------
uint16_t PalettedChunk::read_index(uint32_t i) const noexcept {
    if (bits_ == 0) return 0;  // uniform; only palette[0] is meaningful
    const size_t bit_pos = static_cast<size_t>(i) * bits_;
    const size_t byte_pos = bit_pos / 8;
    if (bits_ == 16) {
        uint16_t v;
        std::memcpy(&v, &indices_[byte_pos], 2);
        return v;
    }
    if (bits_ == 8) {
        return indices_[byte_pos];
    }
    // bits = 1, 2, 4. Read one byte; extract.
    const uint32_t shift = static_cast<uint32_t>(bit_pos & 7u);
    const uint8_t  mask  = static_cast<uint8_t>((1u << bits_) - 1u);
    return static_cast<uint16_t>((indices_[byte_pos] >> shift) & mask);
}

void PalettedChunk::write_index(uint32_t i, uint16_t idx) noexcept {
    const size_t bit_pos = static_cast<size_t>(i) * bits_;
    const size_t byte_pos = bit_pos / 8;
    if (bits_ == 16) {
        std::memcpy(&indices_[byte_pos], &idx, 2);
        return;
    }
    if (bits_ == 8) {
        indices_[byte_pos] = static_cast<uint8_t>(idx);
        return;
    }
    const uint32_t shift = static_cast<uint32_t>(bit_pos & 7u);
    const uint8_t  mask  = static_cast<uint8_t>((1u << bits_) - 1u);
    uint8_t v = indices_[byte_pos];
    v = static_cast<uint8_t>(v & ~(mask << shift));
    v = static_cast<uint8_t>(v | ((idx & mask) << shift));
    indices_[byte_pos] = v;
}

// -----------------------------------------------------------------------------
// grow_bits — expand storage to a wider bits-per-index layout. Called only
// when intern_palette decides the current width can't hold the new entry.
// -----------------------------------------------------------------------------
void PalettedChunk::grow_bits(uint8_t new_bits) {
    assert(new_bits > bits_);
    const uint8_t old_bits = bits_;

    std::vector<uint8_t> new_indices(indices_bytes(new_bits), 0);

    if (old_bits != 0) {
        // Transcode every voxel. Read from the OLD buffer (indices_) manually
        // with old_bits width; write into the NEW buffer (new_indices)
        // manually with new_bits width. We deliberately do not call
        // read_index/write_index here because both depend on bits_ and we
        // would need to straddle two widths in one pass.
        const uint8_t old_mask = static_cast<uint8_t>((1u << old_bits) - 1u);
        const uint8_t new_mask = static_cast<uint8_t>((1u << new_bits) - 1u);
        for (uint32_t i = 0; i < kChunkVoxels; ++i) {
            uint16_t idx;
            if (old_bits == 16) {
                std::memcpy(&idx, &indices_[static_cast<size_t>(i) * 2], 2);
            } else if (old_bits == 8) {
                idx = indices_[i];
            } else {
                const size_t bp = static_cast<size_t>(i) * old_bits;
                const uint8_t sh = static_cast<uint8_t>(bp & 7u);
                idx = static_cast<uint16_t>((indices_[bp >> 3] >> sh) & old_mask);
            }
            if (new_bits == 16) {
                std::memcpy(&new_indices[static_cast<size_t>(i) * 2], &idx, 2);
            } else if (new_bits == 8) {
                new_indices[i] = static_cast<uint8_t>(idx);
            } else {
                const size_t bp = static_cast<size_t>(i) * new_bits;
                const uint8_t sh = static_cast<uint8_t>(bp & 7u);
                uint8_t v = new_indices[bp >> 3];
                v = static_cast<uint8_t>(v & ~(new_mask << sh));
                v = static_cast<uint8_t>(v | ((idx & new_mask) << sh));
                new_indices[bp >> 3] = v;
            }
        }
    }
    // else: promoting from uniform. new_indices is all zeros, which already
    // resolves to palette[0] at every position. Nothing to transcode.

    bits_    = new_bits;
    indices_ = std::move(new_indices);
}

// -----------------------------------------------------------------------------
// intern_palette — return the palette index for block_id, inserting a new
// entry (and growing bits) if necessary. Deterministic insertion order.
// -----------------------------------------------------------------------------
uint16_t PalettedChunk::intern_palette(uint16_t block_id) {
    for (size_t i = 0; i < palette_.size(); ++i) {
        if (palette_[i] == block_id) return static_cast<uint16_t>(i);
    }
    const uint16_t new_idx = static_cast<uint16_t>(palette_.size());
    palette_.push_back(block_id);
    const uint8_t need_bits = bits_for_palette(palette_.size());
    if (need_bits > bits_) grow_bits(need_bits);
    return new_idx;
}

// -----------------------------------------------------------------------------
// get / set
// -----------------------------------------------------------------------------
uint16_t PalettedChunk::get(uint32_t x, uint32_t y, uint32_t z) const noexcept {
    if (bits_ == 0) return palette_[0];
    const uint16_t idx = read_index(index_of(x, y, z));
    return (idx < palette_.size()) ? palette_[idx] : static_cast<uint16_t>(0);
}

bool PalettedChunk::set(uint32_t x, uint32_t y, uint32_t z, uint16_t block_id) {
    // Uniform fast path: writing the uniform value to any voxel is a no-op.
    if (bits_ == 0 && palette_[0] == block_id) return false;

    // intern_palette is idempotent and side-effectful: if the chunk is
    // currently uniform-air and we're writing stone, this call both inserts
    // stone into the palette AND grows bits_ from 0 to 1 (via grow_bits),
    // allocating the index buffer lazily. We don't need a separate "promote
    // uniform" path — §2.11 rung 1, don't do work we don't need.
    const uint16_t new_pidx = intern_palette(block_id);
    const uint32_t vox_i    = index_of(x, y, z);
    const uint16_t old_pidx = read_index(vox_i);
    if (old_pidx == new_pidx) return false;
    write_index(vox_i, new_pidx);
    return true;
}

void PalettedChunk::fill_uniform(uint16_t block_id) {
    bits_ = 0;
    palette_.clear();
    palette_.push_back(block_id);
    indices_.clear();
    indices_.shrink_to_fit();
}

bool PalettedChunk::fill_box(uint32_t x0, uint32_t y0, uint32_t z0,
                             uint32_t x1, uint32_t y1, uint32_t z1,
                             uint16_t block_id) {
    // Whole-chunk fast path — §2.11 rung 1. Caller hands us x0=0..x1=32 etc.
    if (x0 == 0 && y0 == 0 && z0 == 0 &&
        x1 == kChunkEdge && y1 == kChunkEdge && z1 == kChunkEdge) {
        if (bits_ == 0 && palette_[0] == block_id) return false;
        fill_uniform(block_id);
        return true;
    }
    // Partial fill. Intern once, then write indices directly.
    const uint16_t pidx = intern_palette(block_id);
    bool changed = false;
    for (uint32_t y = y0; y < y1; ++y) {
        for (uint32_t z = z0; z < z1; ++z) {
            for (uint32_t x = x0; x < x1; ++x) {
                const uint32_t i = index_of(x, y, z);
                if (read_index(i) != pidx) {
                    write_index(i, pidx);
                    changed = true;
                }
            }
        }
    }
    return changed;
}

void PalettedChunk::copy_voxels(uint16_t* out) const noexcept {
    if (bits_ == 0) {
        const uint16_t v = palette_[0];
        for (uint32_t i = 0; i < kChunkVoxels; ++i) out[i] = v;
        return;
    }
    for (uint32_t i = 0; i < kChunkVoxels; ++i) {
        const uint16_t pidx = read_index(i);
        out[i] = (pidx < palette_.size()) ? palette_[pidx] : static_cast<uint16_t>(0);
    }
}

size_t PalettedChunk::memory_bytes() const noexcept {
    return palette_.capacity() * sizeof(uint16_t) + indices_.capacity();
}

// -----------------------------------------------------------------------------
// Serialisation format (matches Appendix A §A.4.1):
//   u8   bits_per_index
//   u8   palette_size
//   u16  reserved (0)
//   u16  palette[palette_size]
//   u8   indices[]  (size = kChunkVoxels * bits / 8; 0 bytes if uniform)
// -----------------------------------------------------------------------------
size_t PalettedChunk::serialized_size() const noexcept {
    return 4 + palette_.size() * 2 + indices_.size();
}

void PalettedChunk::serialize_into(uint8_t* out) const noexcept {
    out[0] = bits_;
    out[1] = static_cast<uint8_t>(std::min<size_t>(palette_.size(), 255));
    out[2] = 0; out[3] = 0;
    size_t off = 4;
    for (uint16_t v : palette_) {
        std::memcpy(&out[off], &v, 2);
        off += 2;
    }
    if (!indices_.empty()) {
        std::memcpy(&out[off], indices_.data(), indices_.size());
    }
}

bool PalettedChunk::deserialize_from(const uint8_t* in, size_t size) {
    if (size < 4) return false;
    const uint8_t bits = in[0];
    const uint8_t psz  = in[1];
    if (bits != 0 && bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 16) {
        return false;
    }
    const size_t expected_idx = indices_bytes(bits);
    const size_t want = 4 + static_cast<size_t>(psz) * 2 + expected_idx;
    if (size != want) return false;

    bits_ = bits;
    palette_.clear();
    palette_.reserve(psz ? psz : 1);
    for (size_t i = 0; i < psz; ++i) {
        uint16_t v;
        std::memcpy(&v, &in[4 + i * 2], 2);
        palette_.push_back(v);
    }
    if (palette_.empty()) palette_.push_back(0);

    indices_.assign(expected_idx, 0);
    if (expected_idx) {
        std::memcpy(indices_.data(), &in[4 + psz * 2], expected_idx);
    }
    return true;
}

} // namespace demen::voxel_store
