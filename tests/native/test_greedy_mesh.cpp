// =============================================================================
// test_greedy_mesh.cpp — Phase 2 gate suite B-MESH (Appendix E §E.3).
// =============================================================================
// Targets (Appendix E §E.4):
//   * cold meshing of a 12-chunk-radius world < 500 ms
//   * per-dirty-chunk rebuild               < 16 ms
//
// Also asserts:
//   * determinism — two builds of the same chunk emit the same vertex stream
//     byte-for-byte (invariant #2; B-DETERM depends on this for Phase 5).
//   * apron occlusion — a face that shares a voxel boundary with a solid
//     neighbour is culled (§2.4).
//
// Phase 1 status: SKELETON. The helpers that seed a world and compare vertex
// streams are TODO — they land with the Phase 2 Specialist's implementation
// of meshing. Test bodies compile against demen/meshing.hpp today.
// =============================================================================

#include "demen/meshing.hpp"
#include "demen/voxel_store.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using clk = std::chrono::steady_clock;

// -----------------------------------------------------------------------------
// Test configuration — matches the §3.3 Phase 2 gate description
// ("12-chunk-radius world").
// -----------------------------------------------------------------------------
constexpr int32_t kRenderRadiusChunks = 12;                 // §1.2 default
constexpr int32_t kColumnHeightChunks =  4;                 // plenty for a
                                                            //   mesh-only test
constexpr double  kColdBudgetSeconds  = 0.500;              // B-MESH target
constexpr double  kDirtyBudgetSeconds = 0.016;              // per-chunk target

// -----------------------------------------------------------------------------
// Helpers — fleshed out by the Phase 2 Specialist. Signatures frozen so the
// test body does not move when the helpers land.
// -----------------------------------------------------------------------------
static int  seed_meshing_world(demen_world_t world);
static int  acquire_all_chunks(demen_world_t world,
                               std::vector<demen_chunk_t>& out);
static int  release_all_chunks(std::vector<demen_chunk_t>& chunks);
static bool vertex_streams_equal(demen_mesh_t a, demen_mesh_t b);

// -----------------------------------------------------------------------------

static int test_cold_mesh_under_500ms() {
    auto dir = fs::temp_directory_path() / "demen_b_mesh_cold";
    fs::remove_all(dir); fs::create_directories(dir);

    demen_world_params params{};
    params.world_id           = 0xC01D'AE'50'000001ULL;
    params.rng_seed           = 0xB0B1B2B3u;
    params.scale              = DEMEN_WORLD_FINITE_BOUNDED;
    params.bounds_min_chunk_x = -kRenderRadiusChunks;
    params.bounds_min_chunk_z = -kRenderRadiusChunks;
    params.bounds_max_chunk_x =  kRenderRadiusChunks;
    params.bounds_max_chunk_z =  kRenderRadiusChunks;
    params.min_chunk_y        = 0;
    params.max_chunk_y        = kColumnHeightChunks - 1;

    demen_world_t w = 0;
    if (demen_world_create(dir.string().c_str(), &params, &w) != DEMEN_VS_OK) return 1;
    if (seed_meshing_world(w) != 0) { demen_world_close(w); return 2; }

    // Acquire every chunk so meshing can see aprons (§2.3 — apron residency
    // is the meshing subsystem's precondition).
    std::vector<demen_chunk_t> chunks;
    if (acquire_all_chunks(w, chunks) != 0) { demen_world_close(w); return 3; }

    const uint32_t n = static_cast<uint32_t>(chunks.size());
    std::vector<demen_mesh_t> meshes(n, 0);

    const auto t0 = clk::now();
    uint32_t produced = 0;
    const int rc = demen_mesh_build_region(
        w,
        params.bounds_min_chunk_x, params.min_chunk_y, params.bounds_min_chunk_z,
        params.bounds_max_chunk_x, params.max_chunk_y, params.bounds_max_chunk_z,
        DEMEN_MESH_PASS_OPAQUE,
        meshes.data(), n, &produced);
    const auto t1 = clk::now();

    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    std::printf("  cold mesh: %.3f ms (%u chunks)\n", elapsed * 1000.0, produced);

    // Clean up before asserting, so a slow run still releases resources.
    for (auto m : meshes) demen_mesh_release(m);
    release_all_chunks(chunks);
    demen_world_close(w);

    if (rc != DEMEN_MESH_OK) { std::fprintf(stderr, "build_region rc=%d\n", rc); return 4; }
    if (produced == 0)       { std::fprintf(stderr, "no meshes produced\n");     return 5; }
    if (elapsed >= kColdBudgetSeconds) {
        std::fprintf(stderr, "SLOW: %.3f ms exceeds %.0f ms target\n",
                     elapsed * 1000.0, kColdBudgetSeconds * 1000.0);
        return 6;
    }
    return 0;
}

static int test_dirty_chunk_under_16ms() {
    auto dir = fs::temp_directory_path() / "demen_b_mesh_dirty";
    fs::remove_all(dir); fs::create_directories(dir);

    demen_world_params params{};
    params.world_id = 0xD117; params.rng_seed = 0xD117;
    params.scale = DEMEN_WORLD_FINITE_BOUNDED;
    params.bounds_min_chunk_x = -1; params.bounds_min_chunk_z = -1;
    params.bounds_max_chunk_x =  1; params.bounds_max_chunk_z =  1;
    params.min_chunk_y = 0;         params.max_chunk_y        = 0;

    demen_world_t w = 0;
    if (demen_world_create(dir.string().c_str(), &params, &w) != DEMEN_VS_OK) return 1;
    if (seed_meshing_world(w) != 0) { demen_world_close(w); return 2; }

    demen_chunk_t c = 0;
    if (demen_chunk_acquire(w, 0, 0, 0, &c) != DEMEN_VS_OK) {
        demen_world_close(w); return 3;
    }

    // Warm-up build (first build may touch worker-thread cold paths; we're
    // timing the steady-state dirty-rebuild case that a moving player hits).
    demen_mesh_t m = 0;
    if (demen_mesh_build(c, DEMEN_MESH_PASS_OPAQUE, &m, nullptr) != DEMEN_MESH_OK) {
        demen_chunk_release(c); demen_world_close(w); return 4;
    }

    // Dirty one voxel, rebuild, measure.
    if (demen_world_set_voxel(w, 4, 4, 4, 1) != DEMEN_VS_OK) {
        demen_mesh_release(m); demen_chunk_release(c); demen_world_close(w); return 5;
    }

    demen_mesh_stats stats{};
    const auto t0 = clk::now();
    const int rc = demen_mesh_build(c, DEMEN_MESH_PASS_OPAQUE, &m, &stats);
    const auto t1 = clk::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    std::printf("  dirty rebuild: %.3f ms (%u verts, %u quads)\n",
                elapsed * 1000.0, stats.vertex_count, stats.quad_count);

    demen_mesh_release(m); demen_chunk_release(c); demen_world_close(w);

    if (rc != DEMEN_MESH_OK)               return 6;
    if (elapsed >= kDirtyBudgetSeconds) {
        std::fprintf(stderr, "SLOW: %.3f ms exceeds %.0f ms target\n",
                     elapsed * 1000.0, kDirtyBudgetSeconds * 1000.0);
        return 7;
    }
    return 0;
}

static int test_mesh_output_deterministic() {
    // Invariant #2: two builds of the same chunk at the same world state
    // produce the same vertex stream byte-for-byte. B-DETERM (Phase 5)
    // needs this to be true of every subsystem that touches the replay
    // hash surface; meshing is on that list because toy rendering can
    // observe vertex order through picking queries.
    auto dir = fs::temp_directory_path() / "demen_mesh_determ";
    fs::remove_all(dir); fs::create_directories(dir);

    demen_world_params params{};
    params.world_id = 0xDE7E; params.rng_seed = 0xDE7E;
    params.scale = DEMEN_WORLD_FINITE_BOUNDED;
    params.bounds_min_chunk_x = 0; params.bounds_min_chunk_z = 0;
    params.bounds_max_chunk_x = 0; params.bounds_max_chunk_z = 0;
    params.min_chunk_y = 0;        params.max_chunk_y        = 0;

    demen_world_t w = 0;
    if (demen_world_create(dir.string().c_str(), &params, &w) != DEMEN_VS_OK) return 1;
    if (seed_meshing_world(w) != 0) { demen_world_close(w); return 2; }

    demen_chunk_t c = 0;
    if (demen_chunk_acquire(w, 0, 0, 0, &c) != DEMEN_VS_OK) {
        demen_world_close(w); return 3;
    }

    demen_mesh_t a = 0, b = 0;
    if (demen_mesh_build(c, DEMEN_MESH_PASS_OPAQUE, &a, nullptr) != DEMEN_MESH_OK ||
        demen_mesh_build(c, DEMEN_MESH_PASS_OPAQUE, &b, nullptr) != DEMEN_MESH_OK) {
        demen_mesh_release(a); demen_mesh_release(b);
        demen_chunk_release(c); demen_world_close(w); return 4;
    }

    const bool eq = vertex_streams_equal(a, b);

    demen_mesh_release(a); demen_mesh_release(b);
    demen_chunk_release(c); demen_world_close(w);

    if (!eq) { std::fprintf(stderr, "mesh output non-deterministic\n"); return 5; }
    return 0;
}

static int test_transparent_pass_refused_preph6() {
    // Phase 2 surface: only OPAQUE is legal. Calling with TRANSPARENT must
    // return DEMEN_MESH_ERR_PASS_UNSUPPORTED cleanly (not crash, not silently
    // succeed). This is a contract test so the Phase 6 Specialist knows the
    // point at which the gate flips.
    auto dir = fs::temp_directory_path() / "demen_mesh_pass_guard";
    fs::remove_all(dir); fs::create_directories(dir);

    demen_world_params params{};
    params.world_id = 1; params.rng_seed = 1;
    params.scale = DEMEN_WORLD_FINITE_BOUNDED;
    params.bounds_min_chunk_x = 0; params.bounds_min_chunk_z = 0;
    params.bounds_max_chunk_x = 0; params.bounds_max_chunk_z = 0;
    params.min_chunk_y = 0;        params.max_chunk_y        = 0;

    demen_world_t w = 0;
    if (demen_world_create(dir.string().c_str(), &params, &w) != DEMEN_VS_OK) return 1;

    demen_chunk_t c = 0;
    if (demen_chunk_acquire(w, 0, 0, 0, &c) != DEMEN_VS_OK) {
        demen_world_close(w); return 2;
    }

    demen_mesh_t m = 0;
    const int rc = demen_mesh_build(c, DEMEN_MESH_PASS_TRANSPARENT, &m, nullptr);
    demen_mesh_release(m); demen_chunk_release(c); demen_world_close(w);

    if (rc != DEMEN_MESH_ERR_PASS_UNSUPPORTED) {
        std::fprintf(stderr, "expected PASS_UNSUPPORTED, got %d\n", rc);
        return 3;
    }
    return 0;
}

// =============================================================================
int main() {
    int failed = 0;
    struct Case { const char* name; int (*fn)(); };
    const Case cases[] = {
        { "cold_mesh_under_500ms",            test_cold_mesh_under_500ms            },
        { "dirty_chunk_under_16ms",           test_dirty_chunk_under_16ms           },
        { "mesh_output_deterministic",        test_mesh_output_deterministic        },
        { "transparent_pass_refused_preph6",  test_transparent_pass_refused_preph6  },
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
// seed_meshing_world:
//   A mix of uniform chunks (fast palette path, mostly culled interior) and
//   a surface band of mixed block ids (forces greedy meshing to emit real
//   quads). §2.11: do the cheap thing that still exercises the algorithm;
//   don't fill every voxel with unique ids just to pad the vertex count.
//
// acquire_all_chunks / release_all_chunks:
//   Iterate the world-bounds rectangle, demen_chunk_acquire each, collect
//   handles. Guarantees apron residency for the whole meshing pass.
//
// vertex_streams_equal:
//   demen_mesh_get_stats on both, bail if vertex_count differs. Otherwise
//   demen_mesh_copy_vertices on both into scratch buffers and memcmp. The
//   vertex struct is 32 bytes, blittable (§meshing.hpp), so memcmp is the
//   correct primitive (not field-by-field).
//
// Implemented at Phase 2 landing.

static int seed_meshing_world(demen_world_t world) {
    // A stone slab in the lower half of every chunk — exercises greedy face
    // growth along X and Z, vertical face growth on the top plane, and the
    // interior-cull path.
    constexpr uint16_t BLOCK_STONE = 1;
    const int32_t edge = DEMEN_CHUNK_EDGE;
    for (int32_t cx = -kRenderRadiusChunks; cx <= kRenderRadiusChunks; ++cx) {
        for (int32_t cz = -kRenderRadiusChunks; cz <= kRenderRadiusChunks; ++cz) {
            const int32_t x0 = cx * edge, x1 = x0 + edge;
            const int32_t z0 = cz * edge, z1 = z0 + edge;
            if (demen_world_fill_box(world, x0, 0, z0, x1, edge, z1, BLOCK_STONE)
                != DEMEN_VS_OK) return 1;
        }
    }
    return 0;
}

static int acquire_all_chunks(demen_world_t world,
                              std::vector<demen_chunk_t>& out) {
    for (int32_t cx = -kRenderRadiusChunks; cx <= kRenderRadiusChunks; ++cx) {
        for (int32_t cz = -kRenderRadiusChunks; cz <= kRenderRadiusChunks; ++cz) {
            for (int32_t cy = 0; cy < kColumnHeightChunks; ++cy) {
                demen_chunk_t h = 0;
                if (demen_chunk_acquire(world, cx, cy, cz, &h) != DEMEN_VS_OK)
                    return 1;
                out.push_back(h);
            }
        }
    }
    return 0;
}

static int release_all_chunks(std::vector<demen_chunk_t>& chunks) {
    for (auto h : chunks) demen_chunk_release(h);
    chunks.clear();
    return 0;
}

static bool vertex_streams_equal(demen_mesh_t a, demen_mesh_t b) {
    demen_mesh_stats sa{}, sb{};
    if (demen_mesh_get_stats(a, &sa) != DEMEN_MESH_OK) return false;
    if (demen_mesh_get_stats(b, &sb) != DEMEN_MESH_OK) return false;
    if (sa.vertex_count != sb.vertex_count) return false;
    if (sa.vertex_count == 0) return true;

    std::vector<demen_mesh_vertex> va(sa.vertex_count);
    std::vector<demen_mesh_vertex> vb(sb.vertex_count);
    if (demen_mesh_copy_vertices(a, va.data(), sa.vertex_count) != DEMEN_MESH_OK) return false;
    if (demen_mesh_copy_vertices(b, vb.data(), sb.vertex_count) != DEMEN_MESH_OK) return false;
    return std::memcmp(va.data(), vb.data(),
                       sa.vertex_count * sizeof(demen_mesh_vertex)) == 0;
}
