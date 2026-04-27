// =============================================================================
// wave_solver.hpp — §2.5.4. 2D linear wave equation on a heightfield, driven
// by (wind · surface normal) and entity impacts.
// =============================================================================
#pragma once

#include "grid.hpp"

namespace demen::fluid {

// Advance the wave heightfield by `dt` with damping coefficient `damp`.
// `c` is wave speed in metres/second (≈ sqrt(g * depth)).
void step_waves(Grid3D<WaterCell>& w, float dt, float c, float damp);

// Force the height at (gx, gz) by `amount` — used for entity impacts.
void impulse(Grid3D<WaterCell>& w, int gx, int gz, float amount);

// Blend wind into the heightfield as a pressure-like forcing term.
void apply_wind_forcing(Grid3D<WaterCell>& w,
                        const Grid3D<AirCell>& air,
                        float k);

} // namespace demen::fluid
