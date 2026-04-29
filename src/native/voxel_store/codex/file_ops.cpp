#include "file_ops.hpp"

#include <filesystem>
#include <stdexcept>

namespace demen::codex {

std::filesystem::path prepare_temp_world_directory() {
    const std::filesystem::path world_dir = std::filesystem::temp_directory_path() / "demen_codex_voxel_grid_debug";
    std::error_code ec;
    std::filesystem::remove_all(world_dir, ec);
    std::filesystem::create_directories(world_dir, ec);
    if (ec) {
        throw std::runtime_error("Failed to prepare temporary world directory");
    }
    return world_dir;
}

}  // namespace demen::codex
