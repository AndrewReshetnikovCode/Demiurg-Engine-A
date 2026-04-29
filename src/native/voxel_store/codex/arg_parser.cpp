#include "arg_parser.hpp"

#include <algorithm>
#include <string>

namespace demen::codex {

namespace {
int parse_or(const char* v, int fallback) {
    try { return std::stoi(v); } catch (...) { return fallback; }
}
}

grid_config parse_grid_config(int argc, char** argv) {
    grid_config cfg{};
    if (argc > 1) { cfg.mode = argv[1]; }
    if (argc > 2) { cfg.size_x = parse_or(argv[2], cfg.size_x); }
    if (argc > 3) { cfg.size_z = parse_or(argv[3], cfg.size_z); }
    if (argc > 4) { cfg.render_layer_y = parse_or(argv[4], cfg.render_layer_y); }
    if (argc > 5) { cfg.liquid_fill_height = parse_or(argv[5], cfg.liquid_fill_height); }
    cfg.size_x = std::clamp(cfg.size_x, 1, 32);
    cfg.size_y = std::clamp(cfg.size_y, 1, 32);
    cfg.size_z = std::clamp(cfg.size_z, 1, 32);
    if (cfg.render_layer_y >= 0) {
        cfg.render_layer_y = std::clamp(cfg.render_layer_y, 0, cfg.size_y - 1);
    }
    if (cfg.liquid_fill_height >= 0) {
        cfg.liquid_fill_height = std::clamp(cfg.liquid_fill_height, 0, cfg.size_y);
    }
    return cfg;
}

}  // namespace demen::codex
