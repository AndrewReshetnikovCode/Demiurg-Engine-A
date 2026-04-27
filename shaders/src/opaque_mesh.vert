#version 460
// =============================================================================
// opaque_mesh.vert — Phase 3 opaque-pass vertex shader.
// Inputs match demen_mesh_vertex (32 bytes).
// =============================================================================
layout(location = 0) in ivec3 v_pos;        // voxel-local [0, 32]
layout(location = 1) in uvec2 v_face_ao;    // x = normal_face (0..5), y = ao
layout(location = 2) in ivec2 v_uv;         // tile-local fixed-point
layout(location = 3) in uint  v_atlas_slot; // bindless slot index

layout(push_constant) uniform PC {
    mat4  mvp;
    float time_seconds;
    float atlas_override;
    float _pad0;
    float _pad1;
} pc;

layout(location = 0) out vec2  f_uv;
layout(location = 1) out float f_ao;
layout(location = 2) flat out uint f_slot;
layout(location = 3) out vec3  f_world_pos;

void main() {
    vec3 p = vec3(v_pos);           // local voxel coords, 1 voxel = 1 unit
    f_world_pos = p;
    gl_Position = pc.mvp * vec4(p, 1.0);
    // tile_size = 128 (compile-time; §2.10). Fixed-point -> float UV.
    f_uv  = vec2(v_uv) / 128.0;
    f_ao  = float(v_face_ao.y) / 255.0;
    f_slot = (pc.atlas_override < 4294967000.0)
             ? uint(pc.atlas_override) : v_atlas_slot;
}
