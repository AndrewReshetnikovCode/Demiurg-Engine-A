// =============================================================================
// greedy_mesher.hpp — §2.4 greedy meshing on quads, one pass per axis-direction
// (6 total). SIMD acceleration is a later step (§2.11 rung 7); the Phase-2
// baseline is correct plain-C++ first. B-MESH targets drive whether we climb
// the §2.11 ladder later.
// =============================================================================
#pragma once

#include "chunk_view.hpp"
#include "demen/meshing.hpp"

#include <cstdint>
#include <vector>

namespace demen::meshing {

struct MeshBuffer {
    std::vector<demen_mesh_vertex> verts;
    std::vector<uint32_t>          idx;
    demen_mesh_stats               stats{};
};

// Build the opaque mesh for a chunk. Writes into the caller-provided
// MeshBuffer; the vector capacity is preserved between calls so repeated
// rebuilds reuse the underlying storage (§1 invariant #1 — no per-call
// reallocation when the buffer already has room).
//
// `block_id_to_slot` is a callback mapping a voxel's block_id to an atlas
// slot. The meshing subsystem does not own material data; the renderer
// (Phase 3) plugs in a lookup populated from the texture_composition atlas.
// A null callback means "atlas slot 0 for everything" — used by the
// determinism test which does not care about texture identity.
using BlockToSlot = uint32_t (*)(uint16_t block_id, void* user);

void build_opaque_mesh(const ChunkView& v,
                       MeshBuffer& out,
                       BlockToSlot cb,
                       void* cb_user) noexcept;

} // namespace demen::meshing
