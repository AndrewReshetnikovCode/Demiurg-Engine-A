// =============================================================================
// texraw.cpp — Appendix H §H.8 loader. Single memcpy, no allocator games.
// =============================================================================
#include "texraw.hpp"

#include <cstring>
#include <fstream>

namespace demen::texture_composition {

bool load_texraw(const std::filesystem::path& path, TexRaw& out) noexcept {
    try {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;

        uint8_t hdr[16];
        f.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
        if (!f || f.gcount() != 16) return false;

        if (hdr[0] != 'D' || hdr[1] != 'E' || hdr[2] != 'T' || hdr[3] != 'X')
            return false;

        uint32_t w = 0, h = 0;
        std::memcpy(&w, hdr + 4, 4);
        std::memcpy(&h, hdr + 8, 4);
        const uint8_t channels = hdr[12];
        if (hdr[13] != 0 || hdr[14] != 0 || hdr[15] != 0) return false;
        if (channels != 3 && channels != 4) return false;
        if (w == 0 || h == 0 || w > 4096 || h > 4096) return false;

        const size_t pixel_bytes = static_cast<size_t>(w) * h * channels;
        out.pixels.resize(pixel_bytes);
        f.read(reinterpret_cast<char*>(out.pixels.data()),
               static_cast<std::streamsize>(pixel_bytes));
        if (!f || static_cast<size_t>(f.gcount()) != pixel_bytes) {
            out.pixels.clear();
            return false;
        }
        out.width    = w;
        out.height   = h;
        out.channels = channels;
        return true;
    } catch (...) {
        return false;  // ABI boundary — no exceptions escape.
    }
}

} // namespace demen::texture_composition
