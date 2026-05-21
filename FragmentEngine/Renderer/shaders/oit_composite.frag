#version 450

layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D accumTex;
layout(set = 0, binding = 1) uniform sampler2D revealTex;

void main() {
    vec4  accum  = texture(accumTex,  inUV);
    float reveal = texture(revealTex, inUV).r;

    // When accum.a == 0 there were no transparent fragments: alpha = (1 - reveal) = 0,
    // so the alpha blend pass-through makes this a no-op without an explicit discard.
    vec3 avgColor = accum.rgb / clamp(accum.a, 0.00001, 50000.0);
    outColor = vec4(avgColor, 1.0 - reveal);
}
