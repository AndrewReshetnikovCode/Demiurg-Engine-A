// =============================================================================
// water_ca.hpp — bulk water flow. §2.5.4 first paragraph.
// 5 Hz cellular-automata: each water voxel looks downhill in a fixed
// scanline order; up to 1 voxel of water drops into the lower neighbour.
// Direction alternates each tick to kill directional bias.
// =============================================================================
#pragma once

#include "demen/voxel_store.hpp"

namespace demen::fluid {

// Run one CA pass over the chunk-region bounded by [cx_min..cx_max] X
// [cz_min..cz_max]. tick parity (0 or 1) chooses the sweep direction.
void water_ca_step(demen_world_t world,
                   int32_t cx_min, int32_t cz_min,
                   int32_t cx_max, int32_t cz_max,
                   int32_t y_min,  int32_t y_max,
                   uint32_t tick);

} // namespace demen::fluid
