// =============================================================================
// weather_cycle.hpp — §2.5.5 + Phase 7. Evolves the macro-grid: cloud cover,
// precipitation, pressure gradients, mean wind. Runs at the macro 1 Hz tick.
//
// Determinism: every update reads the previous-step buffer (double-buffered)
// and writes into a fresh one; no in-place updates. Macro grid is small
// (4x4 at Phase 7 default) so the copy cost is negligible.
// =============================================================================
#pragma once

#include "grid.hpp"

namespace demen::fluid {

// Advance the weather grid by one macro tick. Drift pressure toward
// equilibrium, advect cloud cover along mean wind, rain-out humidity when
// cloud cover crosses threshold.
void step_weather_cycle(Grid3D<WeatherCell>& w);

} // namespace demen::fluid
