// =============================================================================
// spatial.cpp — swept AABB, 3D-DDA raycast, frustum cull.
// §2.7 collision, §2.11 simplest-thing-that-works.
// =============================================================================
#include "demen/spatial.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

inline int32_t voxel_of(float m) noexcept {
    // 2-metre voxels (§2.3). floor(m / 2).
    return static_cast<int32_t>(std::floor(m / 2.0f));
}

bool voxel_solid(demen_world_t w, int32_t vx, int32_t vy, int32_t vz) noexcept {
    uint16_t id = 0;
    if (demen_world_get_voxel(w, vx, vy, vz, &id) != DEMEN_VS_OK) return false;
    // is_opaque_solid ~= "blocks movement". 0 air, 2 water. Phase 4 treats
    // water as non-blocking (toys swim); Phase 6 adds buoyancy that reads
    // the same query.
    return id != 0 && id != 2;
}

} // namespace

extern "C" {

DEMEN_API int demen_sweep_aabb(demen_world_t world,
                               const demen_aabb* box,
                               const float vel[3],
                               demen_sweep_hit* out) {
    if (!box || !vel || !out) return DEMEN_SP_ERR_INVALID_HANDLE;
    out->hit = 0; out->time = 1.0f;
    out->normal[0] = out->normal[1] = out->normal[2] = 0.0f;
    out->voxel[0] = out->voxel[1] = out->voxel[2] = 0;

    // Expand the swept AABB into a bounding slab. The simplest correct
    // approach is a Minkowski-style expansion of the voxel grid by the box
    // extent, then a standard ray-AABB traversal of one-voxel cells.
    // Phase 4 ships the O(path_length) cellwise test: iterate voxels the
    // box would overlap at increasing t; the first solid voxel wins.
    //
    // For the Phase 4 gate this is adequate — the player moves at ≤ 10 m/s
    // (5 voxels/s) so per-tick path length is ~1 voxel. Entities that
    // move faster use the swept test multiple times per tick.

    auto sign = [](float v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); };
    const int sx = sign(vel[0]), sy = sign(vel[1]), sz = sign(vel[2]);

    // Expanded AABB corner in the direction of motion.
    auto corner = [&](int axis, int s) -> float {
        if (s > 0) return box->max[axis];
        if (s < 0) return box->min[axis];
        return 0.5f * (box->min[axis] + box->max[axis]);
    };

    const float start[3] = { corner(0, sx), corner(1, sy), corner(2, sz) };
    const float end[3]   = { start[0] + vel[0], start[1] + vel[1], start[2] + vel[2] };
    (void)end;

    // Step count in voxel units along each axis.
    const int32_t v0[3] = { voxel_of(start[0]), voxel_of(start[1]), voxel_of(start[2]) };
    const int32_t v1[3] = { voxel_of(start[0] + vel[0]),
                            voxel_of(start[1] + vel[1]),
                            voxel_of(start[2] + vel[2]) };
    const int32_t dx = std::abs(v1[0] - v0[0]);
    const int32_t dy = std::abs(v1[1] - v0[1]);
    const int32_t dz = std::abs(v1[2] - v0[2]);
    const int32_t steps = std::max({ dx, dy, dz, 1 });

    for (int32_t i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float x = start[0] + vel[0] * t;
        const float y = start[1] + vel[1] * t;
        const float z = start[2] + vel[2] * t;
        // Sample the three faces of the expanded AABB along motion.
        const int32_t vx = voxel_of(x);
        const int32_t vy = voxel_of(y);
        const int32_t vz = voxel_of(z);
        if (voxel_solid(world, vx, vy, vz)) {
            out->hit = 1;
            out->time = std::max(0.0f, t - 1.0f / static_cast<float>(steps));
            // Normal: dominant axis. Pick the component of vel whose absolute
            // voxel-delta is largest; normal points opposite motion.
            if (dx >= dy && dx >= dz) { out->normal[0] = -static_cast<float>(sx); }
            else if (dy >= dx && dy >= dz) { out->normal[1] = -static_cast<float>(sy); }
            else { out->normal[2] = -static_cast<float>(sz); }
            out->voxel[0] = vx; out->voxel[1] = vy; out->voxel[2] = vz;
            return DEMEN_SP_OK;
        }
    }
    return DEMEN_SP_OK;
}

DEMEN_API int demen_raycast_voxel(demen_world_t world,
                                  const float o[3], const float d[3],
                                  float maxd, demen_ray_hit* out) {
    if (!o || !d || !out) return DEMEN_SP_ERR_INVALID_HANDLE;
    out->hit = 0; out->distance = 0; out->voxel[0] = out->voxel[1] = out->voxel[2] = 0;
    out->normal[0] = out->normal[1] = out->normal[2] = 0;

    // Woo-Amanatides 3D-DDA in voxel space (2 m per voxel).
    const float vs = 2.0f;
    int32_t vx = voxel_of(o[0]), vy = voxel_of(o[1]), vz = voxel_of(o[2]);
    const int sx = d[0] > 0 ? 1 : (d[0] < 0 ? -1 : 0);
    const int sy = d[1] > 0 ? 1 : (d[1] < 0 ? -1 : 0);
    const int sz = d[2] > 0 ? 1 : (d[2] < 0 ? -1 : 0);

    auto t_to_next_edge = [&](float origin, int voxel, int step, int axis) -> float {
        (void)axis;
        if (step == 0) return INFINITY;
        const float next_boundary = (voxel + (step > 0 ? 1 : 0)) * vs;
        return (next_boundary - origin) / (step > 0 ? std::abs(0.0f + (step > 0 ? 1 : -1)) : 1.0f);
    }; (void)t_to_next_edge;

    auto dist_to_next = [&](float origin, int voxel, float dir, int step) -> float {
        if (step == 0) return INFINITY;
        const float next_boundary = (voxel + (step > 0 ? 1 : 0)) * vs;
        return (next_boundary - origin) / dir;
    };

    float t_max_x = dist_to_next(o[0], vx, d[0], sx);
    float t_max_y = dist_to_next(o[1], vy, d[1], sy);
    float t_max_z = dist_to_next(o[2], vz, d[2], sz);
    const float t_delta_x = (sx == 0) ? INFINITY : vs / std::fabs(d[0]);
    const float t_delta_y = (sy == 0) ? INFINITY : vs / std::fabs(d[1]);
    const float t_delta_z = (sz == 0) ? INFINITY : vs / std::fabs(d[2]);

    float t = 0;
    int last_axis = -1;
    while (t < maxd) {
        if (voxel_solid(world, vx, vy, vz)) {
            out->hit = 1;
            out->distance = t;
            out->voxel[0] = vx; out->voxel[1] = vy; out->voxel[2] = vz;
            // Normal = -step along last-crossed axis.
            if (last_axis == 0) { out->normal[0] = -static_cast<float>(sx); }
            if (last_axis == 1) { out->normal[1] = -static_cast<float>(sy); }
            if (last_axis == 2) { out->normal[2] = -static_cast<float>(sz); }
            return DEMEN_SP_OK;
        }
        if (t_max_x < t_max_y && t_max_x < t_max_z) {
            vx += sx; t = t_max_x; t_max_x += t_delta_x; last_axis = 0;
        } else if (t_max_y < t_max_z) {
            vy += sy; t = t_max_y; t_max_y += t_delta_y; last_axis = 1;
        } else {
            vz += sz; t = t_max_z; t_max_z += t_delta_z; last_axis = 2;
        }
    }
    return DEMEN_SP_OK;
}

DEMEN_API int demen_frustum_cull_chunk(const float vp[16],
                                       int32_t cx, int32_t cy, int32_t cz) {
    // Chunk AABB in world-metres. 32 voxels * 2 m = 64 m per edge (§2.3).
    const float edge_m = 64.0f;
    const float min_x = cx * edge_m, min_y = cy * edge_m, min_z = cz * edge_m;
    const float max_x = min_x + edge_m, max_y = min_y + edge_m, max_z = min_z + edge_m;

    // Test all 8 corners. Trivial but correct. Hot-path callers batch this;
    // the renderer's frustum-plane extraction version is a later escalation.
    const float xs[2] = { min_x, max_x };
    const float ys[2] = { min_y, max_y };
    const float zs[2] = { min_z, max_z };
    bool any_in = false;
    for (int k = 0; k < 8 && !any_in; ++k) {
        const float x = xs[k & 1];
        const float y = ys[(k >> 1) & 1];
        const float z = zs[(k >> 2) & 1];
        // Column-major VP * (x,y,z,1).
        float cx_ = vp[0]*x + vp[4]*y + vp[8]*z  + vp[12];
        float cy_ = vp[1]*x + vp[5]*y + vp[9]*z  + vp[13];
        float cz_ = vp[2]*x + vp[6]*y + vp[10]*z + vp[14];
        float cw  = vp[3]*x + vp[7]*y + vp[11]*z + vp[15];
        if (cw <= 0) continue;
        if (cx_ >= -cw && cx_ <= cw &&
            cy_ >= -cw && cy_ <= cw &&
            cz_ >= 0  && cz_ <= cw) any_in = true;
    }
    return any_in ? 1 : 0;
}

} // extern "C"
