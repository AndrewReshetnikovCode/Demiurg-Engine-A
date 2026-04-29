#include "world_fill.hpp"

#include <string>

namespace demen::codex {

int fill_world_with_blocks(demen_world_t world, const debug_grid& grid) {
    constexpr std::uint16_t kGasBlock = 3;
    constexpr std::uint16_t kLiquidBlock = 2;
    constexpr std::uint16_t kSolidBlock = 1;
    constexpr std::uint16_t kEmptyBlock = 0;
    const std::string mode = grid.config.mode;
    const bool is_air_mode = (mode == "air" || mode == "gas");
    const bool is_liquid_mode = (mode == "liquid" || mode == "water");
    const bool is_solid_mode = (mode == "solid" || mode == "solids");
    const int liquid_height = (grid.config.liquid_fill_height >= 0) ? grid.config.liquid_fill_height : grid.config.size_y;

    for (int y = 0; y < grid.config.size_y; ++y) {
        for (int z = 0; z < grid.config.size_z; ++z) {
            for (int x = 0; x < grid.config.size_x; ++x) {
                std::uint16_t block_id = kGasBlock;
                if (is_air_mode) {
                    block_id = kGasBlock;
                } else if (is_liquid_mode) {
                    block_id = (y < liquid_height) ? kLiquidBlock : kEmptyBlock;
                } else if (is_solid_mode) {
                    const auto idx = static_cast<size_t>((y * grid.config.size_z + z) * grid.config.size_x + x);
                    block_id = grid.cells[idx].occupied ? kSolidBlock : kEmptyBlock;
                }
                if (demen_world_set_voxel(world, x, y, z, block_id) != DEMEN_VS_OK) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

}  // namespace demen::codex
