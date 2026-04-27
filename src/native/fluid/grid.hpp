// =============================================================================
// grid.hpp — atmospheric + water grids. §2.5.1.
//
// Atmospheric grid: 1 cell per 4^3 voxels (8 m cube). Stores vec3 velocity,
// pressure, temperature, humidity. Bounded by the world's chunk extents.
//
// Water grid: 1 cell per 2^3 voxels (4 m square). Stores wave-height offset
// + 2D horizontal flow. Overlaid on the top-water surface (§2.5.4).
//
// Weather macro-grid: 1 cell per 16 chunks (~1 km). Stores large-scale
// pressure + cloud cover + precipitation. Drives far-zone wind.
// =============================================================================
#pragma once

#include "demen/voxel_store.hpp"
#include <cstdint>
#include <vector>

namespace demen::fluid {

constexpr int kAirDownsample = 4;             // DESIGN.md §2.5.1 default
constexpr int kWaterDownsample = 2;           // 2^3 voxels per water cell
constexpr float kTileVoxelSize = DEMEN_VOXEL_SIZE_METERS;   // 2.0
constexpr float kAirCellMeters = kTileVoxelSize * kAirDownsample;  // 8 m
constexpr float kWaterCellMeters = kTileVoxelSize * kWaterDownsample; // 4 m

// Bounded 3D grid keyed by cell coords. Row-major (x fastest, then z, then y
// so vertical advection stays cache-friendly).
template <typename T>
struct Grid3D {
    int32_t x0 = 0, y0 = 0, z0 = 0;    // inclusive cell-index origin
    int32_t nx = 0, ny = 0, nz = 0;
    std::vector<T> data;

    void resize(int32_t X0, int32_t Y0, int32_t Z0,
                int32_t NX, int32_t NY, int32_t NZ, const T& zero) {
        x0 = X0; y0 = Y0; z0 = Z0;
        nx = NX; ny = NY; nz = NZ;
        data.assign(static_cast<size_t>(NX) * NY * NZ, zero);
    }
    size_t index(int32_t x, int32_t y, int32_t z) const noexcept {
        const size_t ix = static_cast<size_t>(x - x0);
        const size_t iy = static_cast<size_t>(y - y0);
        const size_t iz = static_cast<size_t>(z - z0);
        return (iz * static_cast<size_t>(ny) + iy) * static_cast<size_t>(nx) + ix;
    }
    bool in_bounds(int32_t x, int32_t y, int32_t z) const noexcept {
        return x >= x0 && x < x0 + nx
            && y >= y0 && y < y0 + ny
            && z >= z0 && z < z0 + nz;
    }
    T& at(int32_t x, int32_t y, int32_t z)             { return data[index(x, y, z)]; }
    const T& at(int32_t x, int32_t y, int32_t z) const { return data[index(x, y, z)]; }
};

// Atmospheric cell state.
struct AirCell {
    float vel[3]    = { 0, 0, 0 };   // m/s
    float pressure  = 101325.0f;     // Pa (sea level)
    float temp_k    = 288.15f;       // K (15 C)
    float humidity  = 0.4f;          // 0..1 fraction
};

// Water-surface grid cell.
struct WaterCell {
    float height    = 0.0f;          // wave displacement in metres
    float velocity  = 0.0f;          // d height / dt for wave integrator
    float flow[2]   = { 0, 0 };      // horizontal flow m/s (Phase 6)
};

// Macro weather cell.
struct WeatherCell {
    float pressure_pa        = 101325.0f;
    float cloud_cover        = 0.3f;
    float precipitation_mm_s = 0.0f;
    float wind_mean[2]       = { 2.0f, 0.5f };   // m/s (x,z)
};

} // namespace demen::fluid
