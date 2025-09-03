#version 450
layout(location = 0) out vec2 outUV;
void main() {
    // Full-screen triangle: vertices at (-1,-1), (3,-1), (-1,3)
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    outUV = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
