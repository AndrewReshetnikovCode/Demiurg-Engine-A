// =============================================================================
// wave_solver.cpp — second-order linear wave eqn, explicit leapfrog.
//   h_tt = c^2 * (h_xx + h_zz) - damp * h_t + forcing
//
// Integrator: leapfrog-like 2-step. Cell arrays are stored directly inside
// WaterCell (height, velocity), so no extra buffers.
// =============================================================================
#include "wave_solver.hpp"

#include <algorithm>
#include <cmath>

namespace demen::fluid {

namespace {

inline float h(const Grid3D<WaterCell>& w, int x, int z) {
    x = std::clamp(x, w.x0, w.x0 + w.nx - 1);
    z = std::clamp(z, w.z0, w.z0 + w.nz - 1);
    return w.at(x, 0, z).height;
}

} // namespace

void step_waves(Grid3D<WaterCell>& w, float dt, float c, float damp) {
    const float ch = c * c * dt / (kWaterCellMeters * kWaterCellMeters);
    for (int z = w.z0; z < w.z0 + w.nz; ++z)
    for (int x = w.x0; x < w.x0 + w.nx; ++x) {
        WaterCell& cell = w.at(x, 0, z);
        const float lap = h(w, x+1, z) + h(w, x-1, z) + h(w, x, z+1) + h(w, x, z-1)
                        - 4.0f * cell.height;
        cell.velocity += ch * lap * dt - damp * cell.velocity * dt;
    }
    for (int z = w.z0; z < w.z0 + w.nz; ++z)
    for (int x = w.x0; x < w.x0 + w.nx; ++x) {
        WaterCell& cell = w.at(x, 0, z);
        cell.height += cell.velocity * dt;
    }
}

void impulse(Grid3D<WaterCell>& w, int gx, int gz, float amount) {
    if (!w.in_bounds(gx, 0, gz)) return;
    w.at(gx, 0, gz).velocity += amount;
}

void apply_wind_forcing(Grid3D<WaterCell>& w, const Grid3D<AirCell>& air, float k) {
    // Push height in the direction of local horizontal wind — proportional
    // to (wind · gradient-h). Tiny k; visual effect only.
    for (int z = w.z0; z < w.z0 + w.nz; ++z)
    for (int x = w.x0; x < w.x0 + w.nx; ++x) {
        // Map water cell to the air cell above. Water cell is 4 m, air cell
        // is 8 m, so two water cells map to one air cell in each horizontal
        // axis. Phase 6 refinement picks a better mapping if B-WATER wants.
        const int ax = air.x0 + (x - w.x0) / 2;
        const int az = air.z0 + (z - w.z0) / 2;
        if (!air.in_bounds(ax, 0, az)) continue;
        const AirCell& a = air.at(std::clamp(ax, air.x0, air.x0 + air.nx - 1), 0,
                                  std::clamp(az, air.z0, air.z0 + air.nz - 1));
        WaterCell& cell = w.at(x, 0, z);
        const float dhx = h(w, x+1, z) - h(w, x-1, z);
        const float dhz = h(w, x, z+1) - h(w, x, z-1);
        cell.velocity += k * (a.vel[0] * dhx + a.vel[2] * dhz);
    }
}

} // namespace demen::fluid
