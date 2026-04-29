#pragma once

#include <cstdint>
#include <vector>

namespace demen::codex {

struct debug_cell {
    char direction;
    float temperature_c;
    float pressure_kpa;
    bool occupied;
};

struct grid_config {
    int size_x = 10;
    int size_y = 10;
    int size_z = 10;
    int render_layer_y = -1; // -1 => top layer
    int liquid_fill_height = -1; // -1 => full height
    const char* mode = "gas";
};

struct debug_grid {
    grid_config config{};
    std::vector<debug_cell> cells{};
};

}  // namespace demen::codex
