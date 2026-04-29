#include "gas_generator.hpp"

#include <random>
#include <string>

namespace demen::codex {

debug_grid generate_debug_grid(const grid_config& cfg) {
    debug_grid grid{};
    grid.config = cfg;
    grid.cells.resize(static_cast<size_t>(cfg.size_x * cfg.size_y * cfg.size_z));

    std::mt19937 rng(4242u);
    std::uniform_int_distribution<int> dir_dist(0, 3);
    std::uniform_real_distribution<float> temp_dist(-10.0f, 35.0f);
    std::uniform_real_distribution<float> pressure_dist(95.0f, 105.0f);

    const char directions[4] = {'^', '>', 'v', '<'};
    const std::string mode = cfg.mode;
    const bool is_solid_mode = (mode == "solid" || mode == "solids");
    std::bernoulli_distribution solid_or_empty(0.5);
    for (int y = 0; y < cfg.size_y; ++y) {
        for (int z = 0; z < cfg.size_z; ++z) {
            for (int x = 0; x < cfg.size_x; ++x) {
                const size_t idx = static_cast<size_t>((y * cfg.size_z + z) * cfg.size_x + x);
                auto& c = grid.cells[idx];
                c.direction = directions[dir_dist(rng)];
                c.temperature_c = temp_dist(rng);
                c.pressure_kpa = pressure_dist(rng);
                c.occupied = is_solid_mode ? solid_or_empty(rng) : true;
            }
        }
    }
    return grid;
}

}  // namespace demen::codex
