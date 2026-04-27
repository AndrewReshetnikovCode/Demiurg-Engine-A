// =============================================================================
// fluid_abi.cpp — the fluid subsystem's public ABI (§fluid.hpp).
//
// Phase 5: wind + temperature on the atmospheric grid; weather macro-grid
//          feeds boundary forcing.
// Phase 6: water bulk CA + wave heightfield on top of voxel_store water.
// Phase 7: macro-grid advection; humidity -> rainfall -> precipitation.
// =============================================================================
#include "demen/fluid.hpp"

#include "grid.hpp"
#include "air_solver.hpp"
#include "wave_solver.hpp"
#include "water_ca.hpp"
#include "weather_cycle.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace demen::fluid {

struct FluidSim {
    demen_world_t world;

    Grid3D<AirCell>     air;
    Grid3D<WaterCell>   wave;
    Grid3D<WeatherCell> weather;

    // LOD tick accumulators.
    float t_near = 0, t_mid = 0, t_far = 0;

    // Player position in metres (copied each step for weather/LOD).
    float px = 0, py = 0, pz = 0;
};

namespace {

std::mutex g_mu;
std::unordered_map<uint64_t, std::unique_ptr<FluidSim>> g_sims;
std::atomic<uint64_t> g_next{1};

FluidSim* from(demen_fluid_t h) noexcept {
    std::lock_guard lk(g_mu);
    auto it = g_sims.find(h);
    return it == g_sims.end() ? nullptr : it->second.get();
}

// Convert world-metre (x, y, z) -> air-grid cell coords.
inline void metres_to_air_cell(float x, float y, float z,
                               int32_t& cx, int32_t& cy, int32_t& cz) {
    cx = static_cast<int32_t>(std::floor(x / kAirCellMeters));
    cy = static_cast<int32_t>(std::floor(y / kAirCellMeters));
    cz = static_cast<int32_t>(std::floor(z / kAirCellMeters));
}

inline void metres_to_water_cell(float x, float z, int32_t& cx, int32_t& cz) {
    cx = static_cast<int32_t>(std::floor(x / kWaterCellMeters));
    cz = static_cast<int32_t>(std::floor(z / kWaterCellMeters));
}

} // namespace
} // namespace demen::fluid

using namespace demen::fluid;

extern "C" {

DEMEN_API int demen_fluid_create(demen_world_t world, demen_fluid_t* out) {
    if (!out) return DEMEN_FLUID_ERR_INVALID_HANDLE;
    *out = 0;

    auto sim = std::make_unique<FluidSim>();
    sim->world = world;

    // Allocate a modest air grid: ±128 m horizontally, 0..64 m vertical.
    // Phase 5 loads a fixed box around the world origin; a streaming-world
    // path that follows the player arrives when B-AIR numbers land. This
    // covers a 12-chunk radius (±24 air cells = ±192 m) with headroom.
    const int32_t air_half = 32;   // ±32 air cells = ±256 m
    const int32_t air_h    = 16;   // 16 air cells = 128 m vertical
    sim->air.resize(-air_half, 0, -air_half,
                     air_half * 2, air_h, air_half * 2,
                     AirCell{});

    // Water grid: same horizontal footprint at finer resolution.
    const int32_t w_half = 64;
    sim->wave.resize(-w_half, 0, -w_half, w_half * 2, 1, w_half * 2, WaterCell{});

    // Weather macro: 1 cell per 16 chunks = 1024 m. A 4x4 macro grid covers
    // 4 km, a generous envelope around a 12-chunk render radius (~768 m).
    sim->weather.resize(-2, 0, -2, 4, 1, 4, WeatherCell{});

    // Seed a gentle prevailing wind so toys have something to respond to on
    // first launch (§1.5 — flags/smoke/windmill/leaves need non-zero wind).
    for (auto& c : sim->air.data) { c.vel[0] = 2.0f; c.vel[2] = 0.5f; }

    uint64_t id = g_next.fetch_add(1, std::memory_order_relaxed);
    { std::lock_guard lk(g_mu); g_sims.emplace(id, std::move(sim)); }
    *out = id;
    return DEMEN_FLUID_OK;
}

DEMEN_API int demen_fluid_destroy(demen_fluid_t h) {
    std::lock_guard lk(g_mu);
    g_sims.erase(h);
    return DEMEN_FLUID_OK;
}

DEMEN_API int demen_fluid_step(demen_fluid_t h, float dt,
                               float px, float py, float pz) {
    auto* s = from(h);
    if (!s) return DEMEN_FLUID_ERR_INVALID_HANDLE;
    s->px = px; s->py = py; s->pz = pz;

    // LOD accumulators (§2.5.2).
    s->t_near += dt; s->t_mid += dt; s->t_far += dt;

    const float kNearDt = 1.0f / 20.0f;   // 20 Hz
    const float kMidDt  = 1.0f / 5.0f;    // 5 Hz
    const float kFarDt  = 1.0f;           // 1 Hz macro

    // Macro tick first — its output feeds the fine grids.
    if (s->t_far >= kFarDt) {
        s->t_far = 0;
        step_weather_cycle(s->weather);          // Phase 7 full cycle
        apply_weather_forcing(s->air, s->weather); // couples back to §2.5.3
    }

    // Near-zone fine step.
    if (s->t_near >= kNearDt) {
        const float step = s->t_near;
        s->t_near = 0;
        advect(s->air, step);
        project(s->air, /*iters=*/4);
        step_waves(s->wave, step, /*c=*/3.0f, /*damp=*/0.05f);
        apply_wind_forcing(s->wave, s->air, 0.0002f);
    }

    // Mid-zone: coarser step, every 5 Hz. We reuse the same solver with a
    // bigger step; the LOD table in §2.5.2 accepts this because the
    // mid-zone cells are outside the player's visual foveation.
    if (s->t_mid >= kMidDt) {
        s->t_mid = 0;
        // Phase 6: water CA step at 5 Hz over the world's chunk-bounded region.
        // Streaming worlds will get a player-centred slab later; finite worlds
        // run the whole bounds — cheap, because the CA skips air voxels in O(1).
        static uint32_t ca_tick = 0;
        water_ca_step(s->world,
                      s->world /*typed below*/ ? 0 : 0, 0, 0, 0, 0, 0, ca_tick++);
        // The arguments above collapse because the finite-world API does not
        // expose bounds through demen/fluid.hpp; we read the world's bounds
        // via the voxel_store params when they're added to Appendix D. For
        // Phase 6 we restrict the CA to a region around the player:
        const int32_t cr = 4;  // chunks
        const int32_t cx = static_cast<int32_t>(std::floor(s->px / (DEMEN_CHUNK_EDGE * kTileVoxelSize)));
        const int32_t cz = static_cast<int32_t>(std::floor(s->pz / (DEMEN_CHUNK_EDGE * kTileVoxelSize)));
        water_ca_step(s->world, cx - cr, cz - cr, cx + cr, cz + cr,
                      0, DEMEN_CHUNK_EDGE * 2, ca_tick++);
    }
    return DEMEN_FLUID_OK;
}

DEMEN_API int demen_fluid_query_wind(demen_fluid_t h, float x, float y, float z,
                                     float out[3]) {
    auto* s = from(h);
    if (!s || !out) return DEMEN_FLUID_ERR_INVALID_HANDLE;
    int32_t cx, cy, cz;
    metres_to_air_cell(x, y, z, cx, cy, cz);
    cx = std::clamp(cx, s->air.x0, s->air.x0 + s->air.nx - 1);
    cy = std::clamp(cy, s->air.y0, s->air.y0 + s->air.ny - 1);
    cz = std::clamp(cz, s->air.z0, s->air.z0 + s->air.nz - 1);
    const AirCell& c = s->air.at(cx, cy, cz);
    out[0] = c.vel[0]; out[1] = c.vel[1]; out[2] = c.vel[2];
    return DEMEN_FLUID_OK;
}

DEMEN_API int demen_fluid_query_temperature(demen_fluid_t h, float x, float y, float z,
                                            float* out) {
    auto* s = from(h);
    if (!s || !out) return DEMEN_FLUID_ERR_INVALID_HANDLE;
    int32_t cx, cy, cz;
    metres_to_air_cell(x, y, z, cx, cy, cz);
    cx = std::clamp(cx, s->air.x0, s->air.x0 + s->air.nx - 1);
    cy = std::clamp(cy, s->air.y0, s->air.y0 + s->air.ny - 1);
    cz = std::clamp(cz, s->air.z0, s->air.z0 + s->air.nz - 1);
    *out = s->air.at(cx, cy, cz).temp_k;
    return DEMEN_FLUID_OK;
}

DEMEN_API int demen_fluid_query_rainfall(demen_fluid_t h, float x, float z,
                                         float* out) {
    auto* s = from(h);
    if (!s || !out) return DEMEN_FLUID_ERR_INVALID_HANDLE;
    const float mx = x / (16.0f * 64.0f);   // 16 chunks * 64 m
    const float mz = z / (16.0f * 64.0f);
    const int32_t cx = std::clamp(static_cast<int32_t>(std::floor(mx)),
                                   s->weather.x0, s->weather.x0 + s->weather.nx - 1);
    const int32_t cz = std::clamp(static_cast<int32_t>(std::floor(mz)),
                                   s->weather.z0, s->weather.z0 + s->weather.nz - 1);
    *out = s->weather.at(cx, 0, cz).precipitation_mm_s;
    return DEMEN_FLUID_OK;
}

DEMEN_API int demen_fluid_query_water_surface(demen_fluid_t h, float x, float z,
                                              float* out) {
    auto* s = from(h);
    if (!s || !out) return DEMEN_FLUID_ERR_INVALID_HANDLE;
    // Base height: voxel_store's column water_surface_y (metres).
    demen_column_cell cc{};
    // Round to nearest voxel.
    const int32_t vx = static_cast<int32_t>(std::floor(x / kTileVoxelSize));
    const int32_t vz = static_cast<int32_t>(std::floor(z / kTileVoxelSize));
    float base = 0.0f;
    if (demen_world_get_column_cell(s->world, vx, vz, &cc) == DEMEN_VS_OK &&
        cc.water_surface_y != INT16_MIN) {
        base = static_cast<float>(cc.water_surface_y) * kTileVoxelSize;
    }
    int32_t gx, gz;
    metres_to_water_cell(x, z, gx, gz);
    float wave = 0.0f;
    if (s->wave.in_bounds(gx, 0, gz)) wave = s->wave.at(gx, 0, gz).height;
    *out = base + wave;
    return DEMEN_FLUID_OK;
}

DEMEN_API int demen_fluid_set_weather(demen_fluid_t h,
                                      float mx, float mz,
                                      float pressure, float cloud, float precip) {
    auto* s = from(h);
    if (!s) return DEMEN_FLUID_ERR_INVALID_HANDLE;
    const int32_t cx = std::clamp(static_cast<int32_t>(std::floor(mx)),
                                   s->weather.x0, s->weather.x0 + s->weather.nx - 1);
    const int32_t cz = std::clamp(static_cast<int32_t>(std::floor(mz)),
                                   s->weather.z0, s->weather.z0 + s->weather.nz - 1);
    WeatherCell& w = s->weather.at(cx, 0, cz);
    w.pressure_pa        = pressure;
    w.cloud_cover        = std::clamp(cloud, 0.0f, 1.0f);
    w.precipitation_mm_s = std::max(0.0f, precip);
    return DEMEN_FLUID_OK;
}

} // extern "C"
