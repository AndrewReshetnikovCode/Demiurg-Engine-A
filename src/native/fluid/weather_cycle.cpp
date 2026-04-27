// =============================================================================
// weather_cycle.cpp — simple macro-scale weather. Enough to make clouds
// drift, rain fall, wind shift. Not a forecast model.
// =============================================================================
#include "weather_cycle.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace demen::fluid {

namespace {

inline const WeatherCell& safe(const Grid3D<WeatherCell>& g, int x, int z) {
    x = std::clamp(x, g.x0, g.x0 + g.nx - 1);
    z = std::clamp(z, g.z0, g.z0 + g.nz - 1);
    return g.at(x, 0, z);
}

} // namespace

void step_weather_cycle(Grid3D<WeatherCell>& w) {
    std::vector<WeatherCell> next(w.data.size());

    for (int z = w.z0; z < w.z0 + w.nz; ++z)
    for (int x = w.x0; x < w.x0 + w.nx; ++x) {
        const WeatherCell& self = w.at(x, 0, z);

        // Advect cloud cover upstream of mean wind, approximately.
        // Convert wind m/s to cells (macro cells are 1024 m, tick is 1 s).
        const float step_frac = 1.0f / 1024.0f;
        const float src_x = x - self.wind_mean[0] * step_frac;
        const float src_z = z - self.wind_mean[1] * step_frac;
        const int sx0 = static_cast<int>(std::floor(src_x));
        const int sz0 = static_cast<int>(std::floor(src_z));
        const float tx = src_x - sx0;
        const float tz = src_z - sz0;
        auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
        const float c00 = safe(w, sx0,   sz0  ).cloud_cover;
        const float c10 = safe(w, sx0+1, sz0  ).cloud_cover;
        const float c01 = safe(w, sx0,   sz0+1).cloud_cover;
        const float c11 = safe(w, sx0+1, sz0+1).cloud_cover;
        const float cloud_in = lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), tz);

        WeatherCell& out = next[w.index(x, 0, z)];
        out = self;
        out.cloud_cover = std::clamp(cloud_in * 0.98f + 0.01f /*humidity growth*/,
                                     0.0f, 1.0f);

        // Rain-out when cloud exceeds 0.6; reduce cloud cover as rain falls.
        if (out.cloud_cover > 0.6f) {
            out.precipitation_mm_s = (out.cloud_cover - 0.6f) * 6.0f;
            out.cloud_cover -= 0.02f;
        } else {
            out.precipitation_mm_s = 0.0f;
        }

        // Pressure relaxes toward sea level.
        out.pressure_pa = self.pressure_pa * 0.999f + 101325.0f * 0.001f;

        // Wind drift: rotate by small angle dependent on pressure gradient.
        float dpx = (safe(w, x+1, z).pressure_pa - safe(w, x-1, z).pressure_pa);
        float dpz = (safe(w, x, z+1).pressure_pa - safe(w, x, z-1).pressure_pa);
        // Geostrophic-ish deflection: wind perpendicular to pressure gradient.
        out.wind_mean[0] = self.wind_mean[0] + (-dpz) * 1e-4f;
        out.wind_mean[1] = self.wind_mean[1] + ( dpx) * 1e-4f;
        // Cap at 20 m/s (storm limit at Layer 1).
        float s = std::sqrt(out.wind_mean[0]*out.wind_mean[0] +
                            out.wind_mean[1]*out.wind_mean[1]);
        if (s > 20.0f) {
            out.wind_mean[0] *= 20.0f / s;
            out.wind_mean[1] *= 20.0f / s;
        }
    }

    w.data = std::move(next);
}

} // namespace demen::fluid
