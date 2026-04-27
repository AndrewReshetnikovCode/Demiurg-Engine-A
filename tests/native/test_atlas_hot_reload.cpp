// =============================================================================
// test_atlas_hot_reload.cpp — Phase 2 gate suite B-TEXGEN (Appendix E §E.3).
// =============================================================================
// Target (Appendix E §E.4):
//   * hot-reload recomposition < 50 ms
//
// Also asserts:
//   * slot stability — a material that kept its name keeps its slot index
//     (Appendix F §F.5). The renderer's bindless table is pinned; if slots
//     renumbered on reload, every meshed chunk would instantly alias to the
//     wrong texture.
//   * clean refusal on empty / corrupt .texraw (§F.2.2; invariant #7).
//
// Phase 1 status: SKELETON. The helper that produces a minimal valid .texraw
// file on disk is TODO — it lands with the Phase 2 Specialist. Test bodies
// compile against demen/texture_composition.hpp today.
// =============================================================================

#include "demen/texture_composition.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using clk = std::chrono::steady_clock;

constexpr double   kHotReloadBudgetSeconds = 0.050;  // B-TEXGEN target
constexpr uint32_t kTileSize               = 128;    // Appendix H §H.4

// -----------------------------------------------------------------------------
// Helpers — filled in by the Phase 2 Specialist. Shapes frozen here so the
// test body does not move when the helpers land.
// -----------------------------------------------------------------------------

// Write a minimally valid .texraw file (Appendix H §H.8) with a flat RGB
// colour, at the given path. Returns 0 on success.
static int forge_texraw(const fs::path& path, uint8_t r, uint8_t g, uint8_t b);

// Write N .texraw files with distinct flat colours into `dir`, stems
// "mat_00".."mat_{N-1}". Returns 0 on success.
static int forge_n_materials(const fs::path& dir, int n);

// -----------------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------------

static int test_atlas_create_basic_slot_mapping() {
    // Appendix F §F.3: Phase 2 slot index = alphabetical stem order.
    auto dir = fs::temp_directory_path() / "demen_tc_basic";
    fs::remove_all(dir); fs::create_directories(dir);

    if (forge_texraw(dir / "bravo.texraw",   10, 10, 10) != 0) return 1;
    if (forge_texraw(dir / "alpha.texraw",   20, 20, 20) != 0) return 2;
    if (forge_texraw(dir / "charlie.texraw", 30, 30, 30) != 0) return 3;

    demen_atlas_t atlas = 0;
    if (demen_atlas_create(dir.string().c_str(), &atlas) != DEMEN_TC_OK) return 4;

    uint32_t slot = UINT32_MAX;
    if (demen_atlas_material_slot(atlas, "alpha",   &slot) != DEMEN_TC_OK ||
        slot != 0) { std::fprintf(stderr, "alpha slot=%u, want 0\n", slot); return 5; }
    if (demen_atlas_material_slot(atlas, "bravo",   &slot) != DEMEN_TC_OK ||
        slot != 1) { std::fprintf(stderr, "bravo slot=%u, want 1\n", slot); return 6; }
    if (demen_atlas_material_slot(atlas, "charlie", &slot) != DEMEN_TC_OK ||
        slot != 2) { std::fprintf(stderr, "charlie slot=%u, want 2\n", slot); return 7; }

    demen_atlas_info info{};
    if (demen_atlas_info_get(atlas, &info) != DEMEN_TC_OK) return 8;
    if (info.tile_size != kTileSize)    { std::fprintf(stderr, "tile_size=%u\n", info.tile_size); return 9; }
    if (info.n_tiles   != 3)            { std::fprintf(stderr, "n_tiles=%u\n", info.n_tiles);    return 10; }
    if (info.n_channels != 4)           return 11;  // composed to RGBA (§tc hpp)

    return demen_atlas_release(atlas) == DEMEN_TC_OK ? 0 : 12;
}

static int test_hot_reload_under_50ms() {
    auto dir = fs::temp_directory_path() / "demen_tc_hot";
    fs::remove_all(dir); fs::create_directories(dir);

    // Nine materials = the Phase 1 shipping count (Appendix H materials.json).
    if (forge_n_materials(dir, 9) != 0) return 1;

    demen_atlas_t atlas = 0;
    if (demen_atlas_create(dir.string().c_str(), &atlas) != DEMEN_TC_OK) return 2;

    // Rewrite one .texraw with new pixel data to force a recomposition.
    if (forge_texraw(dir / "mat_03.texraw", 99, 99, 99) != 0) { demen_atlas_release(atlas); return 3; }

    const auto t0 = clk::now();
    const int rc = demen_atlas_reload(atlas);
    const auto t1 = clk::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    std::printf("  hot reload: %.3f ms\n", elapsed * 1000.0);

    demen_atlas_release(atlas);

    if (rc != DEMEN_TC_OK)                   return 4;
    if (elapsed >= kHotReloadBudgetSeconds) {
        std::fprintf(stderr, "SLOW: %.3f ms exceeds %.0f ms target\n",
                     elapsed * 1000.0, kHotReloadBudgetSeconds * 1000.0);
        return 5;
    }
    return 0;
}

static int test_slot_stability_across_reload() {
    // Appendix F §F.5: a material that kept its name keeps its slot.
    // Regression guard — the renderer's bindless table cannot survive a
    // renumber.
    auto dir = fs::temp_directory_path() / "demen_tc_stable";
    fs::remove_all(dir); fs::create_directories(dir);

    if (forge_texraw(dir / "stone.texraw", 120, 120, 124) != 0) return 1;
    if (forge_texraw(dir / "grass.texraw",  78, 134,  58) != 0) return 2;
    if (forge_texraw(dir / "water.texraw",  48,  96, 158) != 0) return 3;

    demen_atlas_t atlas = 0;
    if (demen_atlas_create(dir.string().c_str(), &atlas) != DEMEN_TC_OK) return 4;

    uint32_t slot_stone_before  = UINT32_MAX;
    uint32_t slot_grass_before  = UINT32_MAX;
    uint32_t slot_water_before  = UINT32_MAX;
    demen_atlas_material_slot(atlas, "stone", &slot_stone_before);
    demen_atlas_material_slot(atlas, "grass", &slot_grass_before);
    demen_atlas_material_slot(atlas, "water", &slot_water_before);

    // Add a new material "sand" — must land at slot 3 (next free), existing
    // slots unchanged.
    if (forge_texraw(dir / "sand.texraw", 214, 196, 142) != 0) {
        demen_atlas_release(atlas); return 5;
    }
    if (demen_atlas_reload(atlas) != DEMEN_TC_OK) { demen_atlas_release(atlas); return 6; }

    uint32_t slot_stone_after = UINT32_MAX;
    uint32_t slot_grass_after = UINT32_MAX;
    uint32_t slot_water_after = UINT32_MAX;
    uint32_t slot_sand        = UINT32_MAX;
    demen_atlas_material_slot(atlas, "stone", &slot_stone_after);
    demen_atlas_material_slot(atlas, "grass", &slot_grass_after);
    demen_atlas_material_slot(atlas, "water", &slot_water_after);
    demen_atlas_material_slot(atlas, "sand",  &slot_sand);

    demen_atlas_release(atlas);

    if (slot_stone_after != slot_stone_before ||
        slot_grass_after != slot_grass_before ||
        slot_water_after != slot_water_before) {
        std::fprintf(stderr, "slot identity drifted on reload\n");
        return 7;
    }
    if (slot_sand == UINT32_MAX) {
        std::fprintf(stderr, "new material 'sand' not registered\n");
        return 8;
    }
    return 0;
}

static int test_corrupt_texraw_refused_cleanly() {
    // §F.2.2 + invariant #7: bad magic / wrong size must return
    // DEMEN_TC_ERR_CORRUPT, not crash. Adjacent to Phase 1's corrupt-region
    // test — same discipline applied to the texture loader.
    auto dir = fs::temp_directory_path() / "demen_tc_corrupt";
    fs::remove_all(dir); fs::create_directories(dir);

    // Valid file so the directory isn't empty (that's a different error code).
    if (forge_texraw(dir / "good.texraw", 10, 10, 10) != 0) return 1;

    // Junk file with wrong magic.
    {
        std::ofstream f(dir / "bad.texraw", std::ios::binary);
        const char junk[64] = "NOPE NOT A DETX FILE";
        f.write(junk, sizeof(junk));
    }

    demen_atlas_t atlas = 0;
    const int rc = demen_atlas_create(dir.string().c_str(), &atlas);
    demen_atlas_release(atlas);  // no-op on null

    if (rc != DEMEN_TC_ERR_CORRUPT) {
        std::fprintf(stderr, "expected CORRUPT, got %d\n", rc);
        return 2;
    }
    return 0;
}

// =============================================================================
int main() {
    int failed = 0;
    struct Case { const char* name; int (*fn)(); };
    const Case cases[] = {
        { "atlas_create_basic_slot_mapping", test_atlas_create_basic_slot_mapping },
        { "hot_reload_under_50ms",           test_hot_reload_under_50ms           },
        { "slot_stability_across_reload",    test_slot_stability_across_reload    },
        { "corrupt_texraw_refused_cleanly",  test_corrupt_texraw_refused_cleanly  },
    };
    for (const auto& c : cases) {
        int rc = c.fn();
        std::printf("  %-38s %s\n", c.name, rc == 0 ? "PASS" : "FAIL");
        if (rc != 0) ++failed;
    }
    return failed == 0 ? 0 : 1;
}

// =============================================================================
// Helpers — Phase 2 Specialist fleshes these out. Shapes are pinned.
// =============================================================================
//
// forge_texraw:
//   Emit Appendix H §H.8 header + kTileSize*kTileSize*3 bytes of flat RGB.
//   Phase 2 writes 3-channel .texraw; the composer widens to RGBA internally.
//
// forge_n_materials:
//   Loop 0..n-1; filename stem "mat_%02d"; colour varies with the index so the
//   composed pixels differ between tiles. Phase 2 doesn't verify content, but
//   B-TEXGEN future-proofs for a pixel-hash comparator (Phase 5 B-DETERM will
//   want one).

static int forge_texraw(const fs::path& path, uint8_t r, uint8_t g, uint8_t b) {
    // Appendix H §H.8: 16-byte header + W*H*C payload.
    //   magic "DETX", width u32 LE, height u32 LE, channels u8 = 3, 3 reserved.
    std::vector<uint8_t> buf;
    buf.reserve(16 + static_cast<size_t>(kTileSize) * kTileSize * 3);
    buf.insert(buf.end(), { 'D','E','T','X' });

    auto push_u32_le = [&](uint32_t v) {
        buf.push_back(static_cast<uint8_t>(v >>  0));
        buf.push_back(static_cast<uint8_t>(v >>  8));
        buf.push_back(static_cast<uint8_t>(v >> 16));
        buf.push_back(static_cast<uint8_t>(v >> 24));
    };
    push_u32_le(kTileSize);     // width
    push_u32_le(kTileSize);     // height
    buf.push_back(3);           // channels
    buf.push_back(0); buf.push_back(0); buf.push_back(0);   // reserved

    const size_t pixel_bytes = static_cast<size_t>(kTileSize) * kTileSize * 3;
    buf.reserve(buf.size() + pixel_bytes);
    for (size_t i = 0; i < pixel_bytes; i += 3) {
        buf.push_back(r); buf.push_back(g); buf.push_back(b);
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return 1;
    f.write(reinterpret_cast<const char*>(buf.data()),
            static_cast<std::streamsize>(buf.size()));
    return f ? 0 : 1;
}

static int forge_n_materials(const fs::path& dir, int n) {
    for (int i = 0; i < n; ++i) {
        char stem[32];
        std::snprintf(stem, sizeof(stem), "mat_%02d.texraw", i);
        const uint8_t c = static_cast<uint8_t>(16 + i * 20);
        if (forge_texraw(dir / stem, c, c, c) != 0) return 1;
    }
    return 0;
}
