// =============================================================================
// water_ca.cpp — cheap bulk water. Not a hydrology sim — just "water flows
// downhill and across level." Phase 6 baseline; Layer 2 replaces it with a
// real hydrology pass.
//
// Determinism: iteration order is lexicographic z-major, alternating the X
// sweep direction by tick parity so one-sided bias cancels every two ticks.
// =============================================================================
#include "water_ca.hpp"
#include <algorithm>

namespace demen::fluid {

namespace {

inline bool is_air(uint16_t id)   { return id == 0; }
inline bool is_water(uint16_t id) { return id == 2; }

// Try to move water from (sx, sy, sz) to (tx, ty, tz). Returns true on success.
bool try_flow(demen_world_t w, int32_t sx, int32_t sy, int32_t sz,
                               int32_t tx, int32_t ty, int32_t tz) {
    uint16_t target = 0;
    if (demen_world_get_voxel(w, tx, ty, tz, &target) != DEMEN_VS_OK) return false;
    if (!is_air(target)) return false;
    if (demen_world_set_voxel(w, sx, sy, sz, 0) != DEMEN_VS_OK) return false;
    if (demen_world_set_voxel(w, tx, ty, tz, 2) != DEMEN_VS_OK) return false;
    return true;
}

} // namespace

void water_ca_step(demen_world_t world,
                   int32_t cx_min, int32_t cz_min,
                   int32_t cx_max, int32_t cz_max,
                   int32_t y_min,  int32_t y_max,
                   uint32_t tick) {
    const int32_t edge = DEMEN_CHUNK_EDGE;
    const int32_t x_min = cx_min * edge;
    const int32_t x_max = (cx_max + 1) * edge;
    const int32_t z_min = cz_min * edge;
    const int32_t z_max = (cz_max + 1) * edge;

    const bool sweep_left = (tick & 1u) == 0;

    // Bottom-up scan: a voxel we've already moved this tick won't re-flow.
    for (int32_t y = y_min; y <= y_max; ++y) {
        for (int32_t z = z_min; z < z_max; ++z) {
            const int32_t x_start = sweep_left ? x_min     : x_max - 1;
            const int32_t x_end   = sweep_left ? x_max     : x_min - 1;
            const int32_t step    = sweep_left ? 1         : -1;
            for (int32_t x = x_start; x != x_end; x += step) {
                uint16_t id = 0;
                if (demen_world_get_voxel(world, x, y, z, &id) != DEMEN_VS_OK) continue;
                if (!is_water(id)) continue;

                // 1. Fall straight down if possible.
                if (try_flow(world, x, y, z, x, y - 1, z)) continue;
                // 2. Spread horizontally if there's an air neighbour at the
                //    same Y that has air below (i.e. an edge to fall off).
                const int32_t dxs[2] = { step, -step };
                for (int dx : dxs) {
                    uint16_t hn = 0;
                    if (demen_world_get_voxel(world, x + dx, y, z, &hn) != DEMEN_VS_OK) continue;
                    if (!is_air(hn)) continue;
                    uint16_t below = 0;
                    if (demen_world_get_voxel(world, x + dx, y - 1, z, &below) != DEMEN_VS_OK) continue;
                    if (is_air(below) || is_water(below)) {
                        if (try_flow(world, x, y, z, x + dx, y, z)) break;
                    }
                }
            }
        }
    }
}

} // namespace demen::fluid
