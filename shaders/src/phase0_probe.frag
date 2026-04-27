#version 450
layout(location = 0) out vec4 outColor;
// Phase 0 probe: clears to a distinguishable "this works" colour so the gate
// screenshot diff can tell a running build from a default black window.
void main() {
    outColor = vec4(0.12, 0.16, 0.22, 1.0);
}
