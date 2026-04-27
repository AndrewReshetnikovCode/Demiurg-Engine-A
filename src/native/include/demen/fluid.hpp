// =============================================================================
// demen/fluid.hpp — public API for the fluid subsystem (§2.5).
// Planner-owned (invariant #4).
//
// Phase 5: atmosphere (wind velocity, temperature, pressure, weather macro-grid).
// Phase 6: water bulk flow + wave heightfield.
// Phase 7: weather cycle (clouds, precipitation, humidity).
//
// Layer 2 readiness point-queries (Appendix G, invariant #6) live here:
//   demen_fluid_query_wind
//   demen_fluid_query_temperature
//   demen_fluid_query_rainfall
//   demen_fluid_query_water_surface
// =============================================================================
#pragma once

#include "demen/abi.hpp"
#include "demen/voxel_store.hpp"

#ifdef __cplusplus
extern "C" {
#endif

#define DEMEN_FLUID_OK                 0
#define DEMEN_FLUID_ERR_OOM            1
#define DEMEN_FLUID_ERR_INVALID_HANDLE 2
#define DEMEN_FLUID_ERR_OUT_OF_BOUNDS  3

typedef uint64_t demen_fluid_t;

/// Create a fluid simulator bound to a world. The sim reads terrain/water
/// state from the voxel_store and owns the atmospheric + wave grids.
DEMEN_API int demen_fluid_create(demen_world_t world, demen_fluid_t* out);
DEMEN_API int demen_fluid_destroy(demen_fluid_t fluid);

/// Advance one sub-step of real time. LOD zones are recomputed relative to
/// `player_*` each call: near zone (≤4 chunks) at 20 Hz, mid (4..12) at 5 Hz,
/// far (>12) from the weather macro-grid only.
DEMEN_API int demen_fluid_step(demen_fluid_t fluid,
                               float dt,
                               float player_x, float player_y, float player_z);

/// Layer 2 readiness queries — each O(1), allocation-free, thread-safe for
/// reads (Appendix G §G.3).
DEMEN_API int demen_fluid_query_wind(demen_fluid_t fluid,
                                     float x, float y, float z,
                                     float out_vec3[3]);

DEMEN_API int demen_fluid_query_temperature(demen_fluid_t fluid,
                                            float x, float y, float z,
                                            float* out_kelvin);

DEMEN_API int demen_fluid_query_rainfall(demen_fluid_t fluid,
                                         float x, float z,
                                         float* out_mm_per_sec);

/// Water surface height in metres at (x, z), derived from the voxel_store
/// column cell plus the local wave heightfield displacement.
DEMEN_API int demen_fluid_query_water_surface(demen_fluid_t fluid,
                                              float x, float z,
                                              float* out_y_metres);

/// Seed/set weather on the macro-grid. Phase 7 drives this automatically;
/// exposed here for deterministic testing and for a future weather editor.
DEMEN_API int demen_fluid_set_weather(demen_fluid_t fluid,
                                      float macro_x, float macro_z,
                                      float pressure_pa,
                                      float cloud_cover_01,
                                      float precipitation_mm_s);

#ifdef __cplusplus
}
#endif
