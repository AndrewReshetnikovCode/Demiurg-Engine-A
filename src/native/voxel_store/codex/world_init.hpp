#pragma once

#include "demen/voxel_store.hpp"

namespace demen::codex {

demen_world_t create_world(const char* world_dir);
int close_world(demen_world_t world);

}  // namespace demen::codex
