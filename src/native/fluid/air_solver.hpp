// =============================================================================
// air_solver.hpp — semi-Lagrangian advection + 4-iter Jacobi projection.
// §2.5.3 notes: no viscosity, no full Poisson convergence, no energy eqn.
// =============================================================================
#pragma once

#include "grid.hpp"

namespace demen::fluid {

// Advect velocity, temperature, humidity by back-tracing sample points.
void advect(Grid3D<AirCell>& g, float dt);

// Approximate pressure projection. Four Jacobi iterations is the budget.
void project(Grid3D<AirCell>& g, int iters);

// Apply weather macro-grid as boundary forcing to the top layer of the air
// grid. Far zone: whole-column velocity blends toward the macro wind mean.
void apply_weather_forcing(Grid3D<AirCell>& g,
                           const Grid3D<WeatherCell>& w);

} // namespace demen::fluid
