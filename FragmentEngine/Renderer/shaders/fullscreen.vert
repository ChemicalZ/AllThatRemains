#version 450

layout(location = 0) out vec2 outUV;

void main() {
    // Full-screen triangle: vertices at (-1,-1), (3,-1), (-1,3)
    outUV       = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
