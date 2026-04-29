#include "world_init.hpp"

#include <stdexcept>

namespace demen::codex {

demen_world_t create_world(const char* world_dir) {
    demen_world_params params{};
    params.world_id = 0xC0DEULL;
    params.rng_seed = 4242u;
    params.scale = DEMEN_WORLD_FINITE_BOUNDED;
    params.bounds_min_chunk_x = 0;
    params.bounds_min_chunk_z = 0;
    params.bounds_max_chunk_x = 0;
    params.bounds_max_chunk_z = 0;
    params.min_chunk_y = 0;
    params.max_chunk_y = 0;

    demen_world_t world = 0;
    if (demen_world_create(world_dir, &params, &world) != DEMEN_VS_OK) {
        throw std::runtime_error("Failed to create world");
    }
    return world;
}

int close_world(demen_world_t world) {
    return demen_world_close(world);
}

}  // namespace demen::codex
