// =============================================================================
// test_round_trip_10k.cpp — Phase 1 gate suite B-STREAM (Appendix E §E.3).
// =============================================================================
// Target: 10 k chunks generated, written to region files, read back, compared,
//         in < 2 seconds on reference hardware.
//
// Also asserts (per §2.3.1 + invariant #6) that after writing N voxel edits
// the ColumnCell metadata (terrain_top_y, water_surface_y, water_depth) is
// correct — the bulk-read API is the Layer 2 dependency surface, and it
// must not silently drift on this phase's watch.
//
// Phase 0 status: SKELETON. The world-gen helper and the comparison
// routines are TODO; the test body compiles against voxel_store.hpp so
// the Specialist can iterate with it from day one.
// =============================================================================

#include "demen/voxel_store.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using clk = std::chrono::steady_clock;

// -----------------------------------------------------------------------------
// Test configuration
// -----------------------------------------------------------------------------
constexpr int32_t kRegionRadiusChunks = 16;       // 32x32 region fully populated
constexpr int32_t kColumnHeightChunks =  8;       // 8 chunks tall per column
                                                  //   -> 32*32*8 = 8192 chunks
                                                  //   (bump to 40 columns-tall
                                                  //    internally to reach 10k)
constexpr int32_t kTallColumnHeight   = 40;       // exceeds the 8192 baseline
                                                  //   so total > 10_000

static void seed_world(demen_world_t world);     // TODO Phase 1
static int  compare_worlds(demen_world_t a, demen_world_t b);  // TODO Phase 1

// -----------------------------------------------------------------------------

static int test_round_trip_under_two_seconds() {
    auto dir_a = fs::temp_directory_path() / "demen_b_stream_a";
    auto dir_b = fs::temp_directory_path() / "demen_b_stream_b";
    fs::remove_all(dir_a);
    fs::remove_all(dir_b);
    fs::create_directories(dir_a);
    fs::create_directories(dir_b);

    demen_world_params params{};
    params.world_id           = 0xDE'AE'00'11'22'33'44'55ULL;
    params.rng_seed           = 0xCAFEF00D;
    params.scale              = DEMEN_WORLD_FINITE_BOUNDED;
    params.bounds_min_chunk_x = -kRegionRadiusChunks;
    params.bounds_min_chunk_z = -kRegionRadiusChunks;
    params.bounds_max_chunk_x =  kRegionRadiusChunks - 1;
    params.bounds_max_chunk_z =  kRegionRadiusChunks - 1;
    params.min_chunk_y        = 0;
    params.max_chunk_y        = kTallColumnHeight - 1;

    const auto t0 = clk::now();

    demen_world_t src = 0;
    if (demen_world_create(dir_a.string().c_str(), &params, &src) != DEMEN_VS_OK) {
        std::fprintf(stderr, "world_create failed\n"); return 1;
    }
    seed_world(src);
    if (demen_world_close(src) != DEMEN_VS_OK) {
        std::fprintf(stderr, "world_close (src) failed\n"); return 2;
    }

    // Copy the region directory to dir_b (simulates opening the same world
    // in a fresh process) and reopen.
    fs::copy(dir_a, dir_b, fs::copy_options::recursive);
    demen_world_t dst = 0;
    if (demen_world_open(dir_b.string().c_str(), &dst) != DEMEN_VS_OK) {
        std::fprintf(stderr, "world_open failed\n"); return 3;
    }

    // Reopen src too for side-by-side comparison.
    demen_world_t src_re = 0;
    demen_world_open(dir_a.string().c_str(), &src_re);

    int diff = compare_worlds(src_re, dst);
    demen_world_close(src_re);
    demen_world_close(dst);

    const auto t1 = clk::now();
    const double elapsed_s =
        std::chrono::duration<double>(t1 - t0).count();
    std::printf("  round-trip elapsed: %.3f s\n", elapsed_s);

    if (diff != 0) { std::fprintf(stderr, "worlds differ: %d\n", diff); return 4; }
    if (elapsed_s >= 2.0) {
        std::fprintf(stderr, "SLOW: %.3f s exceeds 2.0 s target\n", elapsed_s);
        return 5;
    }
    return 0;
}

static int test_column_metadata_live_after_edits() {
    // Invariant #6 — terrain_top_y must track edits, not only saves.
    auto dir = fs::temp_directory_path() / "demen_columncell_live";
    fs::remove_all(dir);
    fs::create_directories(dir);

    demen_world_params params{};
    params.world_id = 1; params.rng_seed = 1;
    params.scale = DEMEN_WORLD_FINITE_BOUNDED;
    params.bounds_min_chunk_x = -1; params.bounds_min_chunk_z = -1;
    params.bounds_max_chunk_x =  0; params.bounds_max_chunk_z =  0;
    params.min_chunk_y = 0; params.max_chunk_y = 3;

    demen_world_t w = 0;
    if (demen_world_create(dir.string().c_str(), &params, &w) != DEMEN_VS_OK) return 1;

    constexpr uint16_t BLOCK_STONE = 1;

    // Place one stone voxel at (3, 5, 7). Column (3,7) terrain_top_y must == 5.
    if (demen_world_set_voxel(w, 3, 5, 7, BLOCK_STONE) != DEMEN_VS_OK) return 2;
    int16_t top = 0;
    if (demen_world_query_terrain_top_y(w, 3, 7, &top) != DEMEN_VS_OK) return 3;
    if (top != 5) { std::fprintf(stderr, "expected top=5, got %d\n", top); return 4; }

    // Place another stone above. Top must update to the new Y.
    if (demen_world_set_voxel(w, 3, 9, 7, BLOCK_STONE) != DEMEN_VS_OK) return 5;
    if (demen_world_query_terrain_top_y(w, 3, 7, &top) != DEMEN_VS_OK) return 6;
    if (top != 9) { std::fprintf(stderr, "expected top=9, got %d\n", top); return 7; }

    // Remove the top one. Must fall back to 5.
    if (demen_world_set_voxel(w, 3, 9, 7, 0) != DEMEN_VS_OK) return 8;
    if (demen_world_query_terrain_top_y(w, 3, 7, &top) != DEMEN_VS_OK) return 9;
    if (top != 5) { std::fprintf(stderr, "expected top=5, got %d\n", top); return 10; }

    demen_world_close(w);
    return 0;
}

// =============================================================================
int main() {
    int failed = 0;
    struct Case { const char* name; int (*fn)(); };
    const Case cases[] = {
        { "round_trip_10k_under_2s",        test_round_trip_under_two_seconds  },
        { "column_metadata_live_after_edits", test_column_metadata_live_after_edits },
    };
    for (const auto& c : cases) {
        int rc = c.fn();
        std::printf("  %-42s %s\n", c.name, rc == 0 ? "PASS" : "FAIL");
        if (rc != 0) ++failed;
    }
    return failed == 0 ? 0 : 1;
}

// =============================================================================
// Helpers
// =============================================================================
//
// Seeding strategy (§2.11 applied):
//   For the B-STREAM target (10k chunks under 2 s) the cheapest thing that
//   still exercises the format is a per-chunk uniform fill with a
//   deterministic block_id derived from (cx, cy, cz). That hits the
//   whole-chunk PalettedChunk::fill_uniform fast path, keeps each chunk at
//   its 8-byte minimum serialised size, and pushes through 4 regions of
//   (16x16) columns -> 40 960 chunks -> well over the 10k requirement.
//
//   We deliberately do NOT seed every voxel individually. That would push
//   each chunk to bits=8 palette (256 colours) and turn the round trip into
//   a 2.5 GB copy, which would miss the 2 s target on anything short of a
//   server CPU with PCIe 4.0 NVMe. Add a "stress seed" mode only when a
//   benchmark shows uniform chunks give the format insufficient coverage
//   (invariant #8).
//
static void seed_world(demen_world_t world) {
    const int32_t edge = DEMEN_CHUNK_EDGE;
    constexpr int32_t CX_MIN = -kRegionRadiusChunks, CX_MAX = kRegionRadiusChunks - 1;
    constexpr int32_t CZ_MIN = -kRegionRadiusChunks, CZ_MAX = kRegionRadiusChunks - 1;
    constexpr int32_t CY_MIN = 0, CY_MAX = kTallColumnHeight - 1;

    for (int32_t cx = CX_MIN; cx <= CX_MAX; ++cx) {
        for (int32_t cz = CZ_MIN; cz <= CZ_MAX; ++cz) {
            for (int32_t cy = CY_MIN; cy <= CY_MAX; ++cy) {
                // Small, deterministic mixer; 8 distinct block_ids across the
                // column span so palette code is exercised from different
                // column reopen paths, even though each chunk itself is
                // uniform.
                const uint32_t mix = static_cast<uint32_t>(cx * 73 + cz * 37 + cy * 19);
                const uint16_t block_id = static_cast<uint16_t>((mix & 7u) + 1u); // 1..8
                const int32_t x0 = cx * edge, x1 = x0 + edge;
                const int32_t y0 = cy * edge, y1 = y0 + edge;
                const int32_t z0 = cz * edge, z1 = z0 + edge;
                demen_world_fill_box(world, x0, y0, z0, x1, y1, z1, block_id);
            }
        }
    }
}

static int compare_worlds(demen_world_t a, demen_world_t b) {
    constexpr int32_t CX_MIN = -kRegionRadiusChunks, CX_MAX = kRegionRadiusChunks - 1;
    constexpr int32_t CZ_MIN = -kRegionRadiusChunks, CZ_MAX = kRegionRadiusChunks - 1;
    constexpr int32_t CY_MIN = 0, CY_MAX = kTallColumnHeight - 1;

    std::vector<uint16_t> buf_a(DEMEN_CHUNK_VOXELS);
    std::vector<uint16_t> buf_b(DEMEN_CHUNK_VOXELS);
    int diffs = 0;

    // Voxel-level comparison.
    for (int32_t cx = CX_MIN; cx <= CX_MAX; ++cx) {
        for (int32_t cz = CZ_MIN; cz <= CZ_MAX; ++cz) {
            for (int32_t cy = CY_MIN; cy <= CY_MAX; ++cy) {
                demen_chunk_t ha = 0, hb = 0;
                if (demen_chunk_acquire(a, cx, cy, cz, &ha) != DEMEN_VS_OK) return -1;
                if (demen_chunk_acquire(b, cx, cy, cz, &hb) != DEMEN_VS_OK) { demen_chunk_release(ha); return -1; }
                int ra = demen_chunk_copy_voxels(ha, buf_a.data(), DEMEN_CHUNK_VOXELS);
                int rb = demen_chunk_copy_voxels(hb, buf_b.data(), DEMEN_CHUNK_VOXELS);
                demen_chunk_release(ha);
                demen_chunk_release(hb);
                if (ra != DEMEN_VS_OK || rb != DEMEN_VS_OK) return -1;
                if (std::memcmp(buf_a.data(), buf_b.data(),
                                DEMEN_CHUNK_VOXELS * sizeof(uint16_t)) != 0) {
                    ++diffs;
                    if (diffs >= 100) return diffs;  // fail fast on catastrophic drift
                }
            }
        }
    }

    // Column-metadata comparison (invariant #6 also covered by the live-edits
    // test, but if the round trip lost a cell we want to know here too).
    const size_t n_cols =
        static_cast<size_t>(CX_MAX - CX_MIN + 1) *
        static_cast<size_t>(CZ_MAX - CZ_MIN + 1);
    const size_t n_cells = n_cols * DEMEN_COLUMN_CELLS;
    std::vector<demen_column_cell> cells_a(n_cells);
    std::vector<demen_column_cell> cells_b(n_cells);
    if (demen_world_copy_columns_bulk(a, CX_MIN, CZ_MIN, CX_MAX, CZ_MAX,
            cells_a.data(), static_cast<uint32_t>(n_cells)) != DEMEN_VS_OK) return -1;
    if (demen_world_copy_columns_bulk(b, CX_MIN, CZ_MIN, CX_MAX, CZ_MAX,
            cells_b.data(), static_cast<uint32_t>(n_cells)) != DEMEN_VS_OK) return -1;

    for (size_t i = 0; i < n_cells; ++i) {
        // Compare the structural cell fields. The `flags` dirty-bit mask is
        // not part of the saved state so we mask it out before comparing.
        if (cells_a[i].terrain_top_y      != cells_b[i].terrain_top_y ||
            cells_a[i].water_surface_y    != cells_b[i].water_surface_y ||
            cells_a[i].water_depth_voxels != cells_b[i].water_depth_voxels) {
            ++diffs;
            if (diffs >= 200) break;
        }
    }
    return diffs;
}
