// =============================================================================
// greedy_mesher.cpp — §2.4. Axis-aligned quad greedy mesher, six passes.
//
// Algorithm (per axis d, per slice s, per direction +/-):
//   1. Build a 32x32 "mask" of (slot, ao) for every face whose voxel on
//      the +d side of the slice is opaque-solid AND the voxel on the -d
//      side is NOT opaque-solid (face is visible). Record the visible
//      side's atlas slot.
//   2. Greedily grow rectangles over identical mask entries.
//   3. Emit one quad per rectangle.
//
// Output is a pure triangle-list index buffer. Winding order is chosen per
// face direction so the front face points outward without shader flips.
// =============================================================================
#include "greedy_mesher.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>

namespace demen::meshing {

namespace {

struct MaskCell {
    uint32_t slot;   // atlas slot; UINT32_MAX means empty
    uint8_t  ao;     // packed AO for this face
    uint8_t  face;   // demen_mesh_face
};
constexpr uint32_t kEmpty = UINT32_MAX;

inline bool eq(const MaskCell& a, const MaskCell& b) noexcept {
    return a.slot == b.slot && a.ao == b.ao && a.face == b.face;
}

// Simple 4-sample AO from corner neighbours. Cheap; a fuller computation
// costs more than it's worth at Layer 1 (§2.11 corollary).
uint8_t corner_ao(const ChunkView& v,
                  int x, int y, int z,
                  int dx, int dy, int dz,
                  int ux, int uy, int uz,
                  int vx, int vy, int vz) noexcept {
    // Look at the three neighbours at the corner of this face quadrant.
    const bool side_u = is_opaque_solid(v.at(x + dx + ux, y + dy + uy, z + dz + uz));
    const bool side_v = is_opaque_solid(v.at(x + dx + vx, y + dy + vy, z + dz + vz));
    const bool corner = is_opaque_solid(v.at(x + dx + ux + vx,
                                             y + dy + uy + vy,
                                             z + dz + uz + vz));
    // Minecraft-ish: two sides blocked => max occlusion.
    const int count = (side_u ? 1 : 0) + (side_v ? 1 : 0) + (corner ? 1 : 0);
    switch (count) {
        case 0: return 255;
        case 1: return 200;
        case 2: return 150;
        default: return 120;
    }
}

void emit_quad(MeshBuffer& out,
               int16_t x0, int16_t y0, int16_t z0,
               int16_t ux, int16_t uy, int16_t uz,
               int16_t vx, int16_t vy, int16_t vz,
               int w, int h,
               demen_mesh_face face,
               uint32_t slot,
               uint8_t ao) noexcept {
    const uint32_t base = static_cast<uint32_t>(out.verts.size());

    auto push = [&](int16_t px, int16_t py, int16_t pz, int16_t uv_u, int16_t uv_v) {
        demen_mesh_vertex vtx{};
        vtx.pos[0]      = px;
        vtx.pos[1]      = py;
        vtx.pos[2]      = pz;
        vtx.normal_face = static_cast<uint8_t>(face);
        vtx.ao          = ao;
        vtx.uv[0]       = uv_u;
        vtx.uv[1]       = uv_v;
        vtx.atlas_slot  = slot;
        vtx._pad        = 0;
        out.verts.push_back(vtx);
    };

    // UV in tile-local fixed-point. Use the w/h span directly so a greedy
    // N-wide quad repeats the tile N times — shader divides by tile_size.
    const int16_t W = static_cast<int16_t>(w);
    const int16_t H = static_cast<int16_t>(h);

    push(x0,                 y0,                 z0,                 0, 0);
    push(static_cast<int16_t>(x0 + ux * W),
         static_cast<int16_t>(y0 + uy * W),
         static_cast<int16_t>(z0 + uz * W),
         W, 0);
    push(static_cast<int16_t>(x0 + ux * W + vx * H),
         static_cast<int16_t>(y0 + uy * W + vy * H),
         static_cast<int16_t>(z0 + uz * W + vz * H),
         W, H);
    push(static_cast<int16_t>(x0 + vx * H),
         static_cast<int16_t>(y0 + vy * H),
         static_cast<int16_t>(z0 + vz * H),
         0, H);

    // Winding — choose so the face points along `face`'s outward normal.
    const bool flip = (face == DEMEN_MESH_FACE_POS_X ||
                       face == DEMEN_MESH_FACE_NEG_Y ||
                       face == DEMEN_MESH_FACE_POS_Z);
    if (!flip) {
        out.idx.push_back(base + 0);
        out.idx.push_back(base + 1);
        out.idx.push_back(base + 2);
        out.idx.push_back(base + 0);
        out.idx.push_back(base + 2);
        out.idx.push_back(base + 3);
    } else {
        out.idx.push_back(base + 0);
        out.idx.push_back(base + 2);
        out.idx.push_back(base + 1);
        out.idx.push_back(base + 0);
        out.idx.push_back(base + 3);
        out.idx.push_back(base + 2);
    }

    out.stats.quad_count++;
}

struct AxisPass {
    int axis;                      // 0=x, 1=y, 2=z
    int dir;                       // +1 or -1
    demen_mesh_face face;
    int dx, dy, dz;                // normal step
    int ux, uy, uz;                // u-axis step on the face plane
    int vx, vy, vz;                // v-axis step on the face plane
};

// Six passes. The `u`/`v` axes for a face are the two non-normal world axes;
// choice of which is u vs v is arbitrary as long as it is consistent.
constexpr AxisPass kPasses[6] = {
    { 0, -1, DEMEN_MESH_FACE_NEG_X, -1, 0, 0,  0, 0, 1,  0, 1, 0 },
    { 0, +1, DEMEN_MESH_FACE_POS_X, +1, 0, 0,  0, 0, 1,  0, 1, 0 },
    { 1, -1, DEMEN_MESH_FACE_NEG_Y,  0, -1, 0, 1, 0, 0,  0, 0, 1 },
    { 1, +1, DEMEN_MESH_FACE_POS_Y,  0, +1, 0, 1, 0, 0,  0, 0, 1 },
    { 2, -1, DEMEN_MESH_FACE_NEG_Z,  0, 0, -1, 1, 0, 0,  0, 1, 0 },
    { 2, +1, DEMEN_MESH_FACE_POS_Z,  0, 0, +1, 1, 0, 0,  0, 1, 0 },
};

void run_pass(const AxisPass& p,
              const ChunkView& v,
              MeshBuffer& out,
              BlockToSlot cb, void* cb_user) noexcept {
    // Coordinates that span the face's plane (u, v). The third coordinate
    // is the slice index along the pass's axis.
    const int slices   = kEdge;       // slices of voxels
    const int plane_u  = kEdge;
    const int plane_v  = kEdge;

    // mask[v * kEdge + u]
    std::array<MaskCell, static_cast<size_t>(kEdge) * kEdge> mask;

    for (int s = 0; s < slices; ++s) {
        mask.fill({kEmpty, 0, 0});

        // Build the mask.
        for (int vv = 0; vv < plane_v; ++vv) {
            for (int uu = 0; uu < plane_u; ++uu) {
                // Translate (s, uu, vv) to world-voxel (x, y, z) inside the chunk.
                const int x = (p.axis == 0 ? s : (p.ux * uu + p.vx * vv));
                const int y = (p.axis == 1 ? s : (p.uy * uu + p.vy * vv));
                const int z = (p.axis == 2 ? s : (p.uz * uu + p.vz * vv));

                // Voxel on the "inside" side of the face (emits the face) and
                // the "outside" side (occludes it if solid).
                const int inside_x  = (p.dir == +1 ? x           : x + p.dx);
                const int inside_y  = (p.dir == +1 ? y           : y + p.dy);
                const int inside_z  = (p.dir == +1 ? z           : z + p.dz);
                const int outside_x = inside_x + p.dx;
                const int outside_y = inside_y + p.dy;
                const int outside_z = inside_z + p.dz;

                const uint16_t id_in  = v.at(inside_x, inside_y, inside_z);
                const uint16_t id_out = v.at(outside_x, outside_y, outside_z);

                const bool face_visible =
                    is_opaque_solid(id_in) && !is_opaque_solid(id_out);
                if (!face_visible) {
                    if (is_opaque_solid(id_in) && is_opaque_solid(id_out)) {
                        out.stats.empty_faces++;  // interior; culled by apron test
                    }
                    continue;
                }

                const uint32_t slot = cb ? cb(id_in, cb_user) : 0;
                const uint8_t ao = corner_ao(v, inside_x, inside_y, inside_z,
                                             p.dx, p.dy, p.dz,
                                             p.ux, p.uy, p.uz,
                                             p.vx, p.vy, p.vz);
                mask[static_cast<size_t>(vv) * kEdge + uu] =
                    { slot, ao, static_cast<uint8_t>(p.face) };
            }
        }

        // Greedy growth.
        for (int vv = 0; vv < plane_v; ++vv) {
            for (int uu = 0; uu < plane_u; ) {
                MaskCell c = mask[static_cast<size_t>(vv) * kEdge + uu];
                if (c.slot == kEmpty) { ++uu; continue; }

                // Grow width.
                int w = 1;
                while (uu + w < plane_u &&
                       eq(mask[static_cast<size_t>(vv) * kEdge + uu + w], c)) ++w;

                // Grow height.
                int h = 1;
                bool done = false;
                while (!done && vv + h < plane_v) {
                    for (int k = 0; k < w; ++k) {
                        if (!eq(mask[static_cast<size_t>(vv + h) * kEdge + uu + k], c)) {
                            done = true; break;
                        }
                    }
                    if (!done) ++h;
                }

                // Anchor: the (x,y,z) position at which the quad begins.
                // For +dir faces, the quad sits on the s+1 plane.
                const int slice_off = (p.dir == +1 ? 1 : 0);
                const int ax = (p.axis == 0 ? s + slice_off
                                             : p.ux * uu + p.vx * vv);
                const int ay = (p.axis == 1 ? s + slice_off
                                             : p.uy * uu + p.vy * vv);
                const int az = (p.axis == 2 ? s + slice_off
                                             : p.uz * uu + p.vz * vv);

                emit_quad(out,
                    static_cast<int16_t>(ax), static_cast<int16_t>(ay), static_cast<int16_t>(az),
                    static_cast<int16_t>(p.ux), static_cast<int16_t>(p.uy), static_cast<int16_t>(p.uz),
                    static_cast<int16_t>(p.vx), static_cast<int16_t>(p.vy), static_cast<int16_t>(p.vz),
                    w, h, p.face, c.slot, c.ao);

                // Zero out the consumed rectangle.
                for (int yy = 0; yy < h; ++yy)
                    for (int xx = 0; xx < w; ++xx)
                        mask[static_cast<size_t>(vv + yy) * kEdge + uu + xx] =
                            { kEmpty, 0, 0 };

                uu += w;
            }
        }
    }
}

} // namespace

void build_opaque_mesh(const ChunkView& v,
                       MeshBuffer& out,
                       BlockToSlot cb, void* cb_user) noexcept {
    const auto t0 = std::chrono::steady_clock::now();

    // Reuse capacity; clear size (§2.11 rung 1 — no reallocation on rebuild).
    out.verts.clear();
    out.idx.clear();
    out.stats = {};

    for (const auto& p : kPasses) {
        run_pass(p, v, out, cb, cb_user);
    }

    out.stats.vertex_count = static_cast<uint32_t>(out.verts.size());
    out.stats.index_count  = static_cast<uint32_t>(out.idx.size());

    const auto t1 = std::chrono::steady_clock::now();
    out.stats.build_nanos = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
}

} // namespace demen::meshing
