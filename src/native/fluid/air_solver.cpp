// =============================================================================
// air_solver.cpp — Stam-style advection + Jacobi projection (§2.5.3).
//
// Determinism (invariant #2): iteration order is fixed (z-major, then y,
// then x). Jacobi iteration reads the previous-step buffer, so there is no
// order-of-update race; this matches the "no thread-schedule-dependent
// reductions" rule in §2.8.
// =============================================================================
#include "air_solver.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace demen::fluid {

namespace {

inline AirCell trilinear_sample(const Grid3D<AirCell>& g, float fx, float fy, float fz) {
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const int z0 = static_cast<int>(std::floor(fz));
    const float tx = fx - x0;
    const float ty = fy - y0;
    const float tz = fz - z0;

    auto sample = [&](int x, int y, int z) -> AirCell {
        x = std::clamp(x, g.x0, g.x0 + g.nx - 1);
        y = std::clamp(y, g.y0, g.y0 + g.ny - 1);
        z = std::clamp(z, g.z0, g.z0 + g.nz - 1);
        return g.at(x, y, z);
    };

    const AirCell c000 = sample(x0,   y0,   z0  );
    const AirCell c100 = sample(x0+1, y0,   z0  );
    const AirCell c010 = sample(x0,   y0+1, z0  );
    const AirCell c110 = sample(x0+1, y0+1, z0  );
    const AirCell c001 = sample(x0,   y0,   z0+1);
    const AirCell c101 = sample(x0+1, y0,   z0+1);
    const AirCell c011 = sample(x0,   y0+1, z0+1);
    const AirCell c111 = sample(x0+1, y0+1, z0+1);

    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    AirCell out{};
    for (int k = 0; k < 3; ++k) {
        float a00 = lerp(c000.vel[k], c100.vel[k], tx);
        float a10 = lerp(c010.vel[k], c110.vel[k], tx);
        float a01 = lerp(c001.vel[k], c101.vel[k], tx);
        float a11 = lerp(c011.vel[k], c111.vel[k], tx);
        float b0  = lerp(a00, a10, ty);
        float b1  = lerp(a01, a11, ty);
        out.vel[k] = lerp(b0, b1, tz);
    }
    float t000 = c000.temp_k, t100 = c100.temp_k, t010 = c010.temp_k, t110 = c110.temp_k;
    float t001 = c001.temp_k, t101 = c101.temp_k, t011 = c011.temp_k, t111 = c111.temp_k;
    float a00 = lerp(t000, t100, tx), a10 = lerp(t010, t110, tx);
    float a01 = lerp(t001, t101, tx), a11 = lerp(t011, t111, tx);
    out.temp_k = lerp(lerp(a00, a10, ty), lerp(a01, a11, ty), tz);
    out.humidity = c000.humidity;  // near-nearest is enough; humidity is low-variance
    out.pressure = c000.pressure;
    return out;
}

} // namespace

void advect(Grid3D<AirCell>& g, float dt) {
    // Back-trace sample. Cell size is 1 unit in grid coords, so the velocity
    // in metres-per-second becomes (m/s) * dt / kAirCellMeters cells-per-step.
    Grid3D<AirCell> out = g;
    const float scale = dt / kAirCellMeters;
    for (int z = g.z0; z < g.z0 + g.nz; ++z)
    for (int y = g.y0; y < g.y0 + g.ny; ++y)
    for (int x = g.x0; x < g.x0 + g.nx; ++x) {
        const AirCell& c = g.at(x, y, z);
        const float sx = static_cast<float>(x) - c.vel[0] * scale;
        const float sy = static_cast<float>(y) - c.vel[1] * scale;
        const float sz = static_cast<float>(z) - c.vel[2] * scale;
        out.at(x, y, z) = trilinear_sample(g, sx, sy, sz);
    }
    g = std::move(out);
}

void project(Grid3D<AirCell>& g, int iters) {
    // Compute divergence at each cell, run Jacobi on pressure, subtract the
    // gradient. This is the cheap version — §2.5.3 allows 4 iterations, not
    // convergence. Phase 5 gate decides whether to raise it.

    std::vector<float> div(g.data.size(), 0.0f);
    std::vector<float> press(g.data.size(), 0.0f);

    auto idx = [&](int x, int y, int z) { return g.index(x, y, z); };
    auto sample_v = [&](int x, int y, int z, int k) -> float {
        x = std::clamp(x, g.x0, g.x0 + g.nx - 1);
        y = std::clamp(y, g.y0, g.y0 + g.ny - 1);
        z = std::clamp(z, g.z0, g.z0 + g.nz - 1);
        return g.at(x, y, z).vel[k];
    };

    for (int z = g.z0; z < g.z0 + g.nz; ++z)
    for (int y = g.y0; y < g.y0 + g.ny; ++y)
    for (int x = g.x0; x < g.x0 + g.nx; ++x) {
        const float dvx = sample_v(x+1, y, z, 0) - sample_v(x-1, y, z, 0);
        const float dvy = sample_v(x, y+1, z, 1) - sample_v(x, y-1, z, 1);
        const float dvz = sample_v(x, y, z+1, 2) - sample_v(x, y, z-1, 2);
        div[idx(x, y, z)] = -0.5f * (dvx + dvy + dvz) * kAirCellMeters;
    }

    for (int it = 0; it < iters; ++it) {
        std::vector<float> next(press.size(), 0.0f);
        for (int z = g.z0; z < g.z0 + g.nz; ++z)
        for (int y = g.y0; y < g.y0 + g.ny; ++y)
        for (int x = g.x0; x < g.x0 + g.nx; ++x) {
            const size_t i = idx(x, y, z);
            auto p = [&](int X, int Y, int Z) -> float {
                X = std::clamp(X, g.x0, g.x0 + g.nx - 1);
                Y = std::clamp(Y, g.y0, g.y0 + g.ny - 1);
                Z = std::clamp(Z, g.z0, g.z0 + g.nz - 1);
                return press[g.index(X, Y, Z)];
            };
            next[i] = (div[i]
                     + p(x-1,y,z) + p(x+1,y,z)
                     + p(x,y-1,z) + p(x,y+1,z)
                     + p(x,y,z-1) + p(x,y,z+1)) / 6.0f;
        }
        press.swap(next);
    }

    // Subtract the pressure gradient from velocity.
    for (int z = g.z0; z < g.z0 + g.nz; ++z)
    for (int y = g.y0; y < g.y0 + g.ny; ++y)
    for (int x = g.x0; x < g.x0 + g.nx; ++x) {
        auto p = [&](int X, int Y, int Z) -> float {
            X = std::clamp(X, g.x0, g.x0 + g.nx - 1);
            Y = std::clamp(Y, g.y0, g.y0 + g.ny - 1);
            Z = std::clamp(Z, g.z0, g.z0 + g.nz - 1);
            return press[g.index(X, Y, Z)];
        };
        AirCell& c = g.at(x, y, z);
        c.vel[0] -= 0.5f * (p(x+1,y,z) - p(x-1,y,z)) / kAirCellMeters;
        c.vel[1] -= 0.5f * (p(x,y+1,z) - p(x,y-1,z)) / kAirCellMeters;
        c.vel[2] -= 0.5f * (p(x,y,z+1) - p(x,y,z-1)) / kAirCellMeters;
        c.pressure = 101325.0f + press[idx(x, y, z)];
    }
}

void apply_weather_forcing(Grid3D<AirCell>& g, const Grid3D<WeatherCell>& w) {
    // Far-zone coupling: the uppermost layer of the air grid blends toward
    // the macro cell's mean wind. Simple linear interpolation maps an air
    // cell (x,y,z) back to a macro cell (x/macro, z/macro). For Phase 5 we
    // pick the nearest macro cell rather than trilinear — macro cells are
    // 1 km wide so the lossy pickup is visually invisible.

    const int y_top = g.y0 + g.ny - 1;
    for (int z = g.z0; z < g.z0 + g.nz; ++z)
    for (int x = g.x0; x < g.x0 + g.nx; ++x) {
        const int mx = w.x0 + (x - g.x0) * g.nx / std::max(1, w.nx * 8);   // rough projection
        const int mz = w.z0 + (z - g.z0) * g.nz / std::max(1, w.nz * 8);
        const int mxc = std::clamp(mx, w.x0, w.x0 + w.nx - 1);
        const int mzc = std::clamp(mz, w.z0, w.z0 + w.nz - 1);
        const WeatherCell& c = w.at(mxc, 0, mzc);
        AirCell& a = g.at(x, y_top, z);
        a.vel[0] = a.vel[0] * 0.9f + c.wind_mean[0] * 0.1f;
        a.vel[2] = a.vel[2] * 0.9f + c.wind_mean[1] * 0.1f;
    }
}

} // namespace demen::fluid
