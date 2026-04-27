// =============================================================================
// chunk_view.cpp — copy 34^3 voxels out of the voxel_store in one sweep.
// =============================================================================
#include "chunk_view.hpp"

namespace demen::meshing {

bool fill_chunk_view(demen_world_t world,
                     int32_t cx, int32_t cy, int32_t cz,
                     ChunkView& view) noexcept {
    const int base_x = cx * kEdge;
    const int base_y = cy * kEdge;
    const int base_z = cz * kEdge;
    for (int z = -1; z <= kEdge; ++z) {
        for (int y = -1; y <= kEdge; ++y) {
            for (int x = -1; x <= kEdge; ++x) {
                uint16_t b = 0;
                (void)demen_world_get_voxel(world,
                    base_x + x, base_y + y, base_z + z, &b);
                view.set(x, y, z, b);
            }
        }
    }
    (void)world;
    return true;
}

} // namespace demen::meshing
