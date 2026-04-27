// =============================================================================
// texraw.hpp — loader for the .texraw companion format (Appendix H §H.8).
// A 20-line memcpy, by spec. Any heavier is a §2.11-rung-1 violation.
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace demen::texture_composition {

struct TexRaw {
    uint32_t width   = 0;
    uint32_t height  = 0;
    uint32_t channels = 0;      // 3 or 4
    std::vector<uint8_t> pixels; // row-major, no stride
};

// Load a .texraw file. Returns true on success; on failure `out` is left
// empty and the caller reports DEMEN_TC_ERR_CORRUPT / _IO.
bool load_texraw(const std::filesystem::path& path, TexRaw& out) noexcept;

} // namespace demen::texture_composition
