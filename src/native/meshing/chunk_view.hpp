// =============================================================================
// chunk_view.hpp — 34^3 voxel view of a chunk plus its 1-voxel apron.
// §2.3 apron; §2.4 feeds greedy meshing.
// =============================================================================
#pragma once

#include "demen/voxel_store.hpp"
#include <array>
#include <cstdint>

namespace demen::meshing {

constexpr int kEdge        = DEMEN_CHUNK_EDGE;    // 32
constexpr int kEdgePlus2   = kEdge + 2;           // 34
constexpr int kEdgePlus2C3 = kEdgePlus2 * kEdgePlus2 * kEdgePlus2; // 39 304

struct ChunkView {
    // (x, y, z) each in [-1, 32]. We store as [0, 33].
    // Layout: v[(z+1) * 34 * 34 + (y+1) * 34 + (x+1)].
    std::array<uint16_t, kEdgePlus2C3> v{};

    static constexpr size_t idx(int x, int y, int z) noexcept {
        return static_cast<size_t>((z + 1) * kEdgePlus2 * kEdgePlus2
                                 + (y + 1) * kEdgePlus2
                                 + (x + 1));
    }
    uint16_t at(int x, int y, int z) const noexcept { return v[idx(x, y, z)]; }
    void set(int x, int y, int z, uint16_t id) noexcept { v[idx(x, y, z)] = id; }
};

// Fill `view` from the world for chunk (cx, cy, cz). Reads the chunk's 32^3
// voxels plus the one-voxel apron from neighbour chunks; missing neighbours
// are filled with air (0). Uses the same demen_world_get_voxel path the ABI
// exposes, so this stays within the public-surface contract (meshing is a
// scope-locked subsystem per §3.2 and cannot poke voxel_store internals).
bool fill_chunk_view(demen_world_t world,
                     int32_t cx, int32_t cy, int32_t cz,
                     ChunkView& view) noexcept;

// A block id is "opaque-solid" if it occludes the adjacent face at Phase 2.
// Layer-2 material flags will replace this inline table; see Appendix F §F.2.
inline bool is_opaque_solid(uint16_t id) noexcept {
    // 0 air; 2 reserved as water (non-opaque). Everything else = opaque.
    return id != 0 && id != 2;
}

} // namespace demen::meshing
