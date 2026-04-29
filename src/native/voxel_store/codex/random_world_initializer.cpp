#include "random_world_initializer.hpp"

#include "arg_parser.hpp"
#include "console_render.hpp"
#include "file_ops.hpp"
#include "gas_generator.hpp"
#include "world_fill.hpp"
#include "world_init.hpp"

#include <iostream>

namespace demen::codex {

int run_random_world_initializer(int argc, char** argv) {
    try {
        const grid_config cfg = parse_grid_config(argc, argv);
        const auto world_dir = prepare_temp_world_directory();
        demen_world_t world = create_world(world_dir.string().c_str());

        const debug_grid grid = generate_debug_grid(cfg);
        if (fill_world_with_blocks(world, grid) != 0) {
            std::cerr << "Failed to fill world with requested block mode\n";
            close_world(world);
            return 1;
        }

        const int layer_y = (cfg.render_layer_y >= 0) ? cfg.render_layer_y : (cfg.size_y - 1);
        std::cout << "Mode: " << cfg.mode << "\n";
        std::cout << "Grid: x=" << cfg.size_x << " y=" << cfg.size_y << " z=" << cfg.size_z << "\n";
        std::cout << "Render layer y=" << layer_y << "\n";
        render_layer(grid, layer_y);

        close_world(world);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}

}  // namespace demen::codex
