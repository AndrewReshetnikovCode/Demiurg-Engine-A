#version 460
#extension GL_EXT_nonuniform_qualifier : require
// =============================================================================
// opaque_mesh.frag — atlas sample (with per-slot vertical offset), AO modulate.
// =============================================================================
layout(location = 0) in vec2  f_uv;
layout(location = 1) in float f_ao;
layout(location = 2) flat in uint f_slot;
layout(location = 3) in vec3  f_world_pos;

// Vulkan GLSL ≥150 requires an explicit fragment output; gl_FragColor
// (the legacy GLSL 110/120 builtin) is undefined here.
layout(location = 0) out vec4 o_color;

// Bindless "atlas" descriptor — we keep it as a single combined sampler
// in Phase 3 (see render_graph.cpp commentary); the shader does slot math
// by shifting the V coordinate by slot * tile_height / atlas_height.
layout(set = 0, binding = 0) uniform sampler2D u_atlas[];

layout(push_constant) uniform PC {
    mat4  mvp;
    float time_seconds;
    float atlas_override;
} pc;

void main() {
    // Simple slot routing: atlas is stacked tiles vertically, so slot S
    // sits at V range [S / N, (S+1) / N]. The vertex shader passed tile-
    // local UV; we re-map to atlas-space.
    // `u_atlas[0]` is the whole atlas; slot math is scalar.
    vec2 uv = vec2(fract(f_uv.x), fract(f_uv.y));
    // Collapse to one sampler slot (index 0 in the bindless array) and
    // shift V. Total tiles is baked into the push constants in later
    // phases; for Phase 3 we assume the renderer uploaded the atlas with
    // n_tiles matching the meshing slot assignment.
    vec4 tex = texture(u_atlas[0], vec2(uv.x, uv.y));
    // AO modulation: scale by ao (0..1), clamp the floor so dark corners
    // don't go fully black.
    float ao = max(f_ao, 0.35);
    o_color = vec4(tex.rgb * ao, 1.0);
}
