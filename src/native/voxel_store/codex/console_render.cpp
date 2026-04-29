#include "console_render.hpp"

#include <iostream>
#include <string>

namespace demen::codex {

void render_layer(const debug_grid& grid, int layer_y) {
    const std::string mode = grid.config.mode;
    const bool is_air_mode = (mode == "air" || mode == "gas");
    const bool is_liquid_mode = (mode == "liquid" || mode == "water");
    const bool is_solid_mode = (mode == "solid" || mode == "solids");
    const int liquid_height = (grid.config.liquid_fill_height >= 0) ? grid.config.liquid_fill_height : grid.config.size_y;
    std::cout << "Layer y=" << layer_y << " (wind direction):\n";
    for (int z = 0; z < grid.config.size_z; ++z) {
        for (int x = 0; x < grid.config.size_x; ++x) {
            const auto idx = static_cast<size_t>((layer_y * grid.config.size_z + z) * grid.config.size_x + x);
            const auto& c = grid.cells[idx];
            if (is_air_mode) {
                std::cout << c.direction;
            } else if (is_liquid_mode) {
                const bool is_surface = (liquid_height > 0 && layer_y == (liquid_height - 1));
                std::cout << (is_surface ? c.direction : 'L');
            } else if (is_solid_mode) {
                std::cout << (c.occupied ? '#' : '.');
            } else {
                std::cout << c.direction;
            }
        }
        std::cout << '\n';
    }
}

}  // namespace demen::codex
