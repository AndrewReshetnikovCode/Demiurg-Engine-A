#version 450
// Phase 0 probe: emits NDC-fullscreen-triangle vertex. Used by the empty
// Vulkan window gate check (§3.3 Phase 0).
void main() {
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
