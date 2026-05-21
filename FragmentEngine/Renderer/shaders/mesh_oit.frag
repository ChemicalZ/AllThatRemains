#version 450

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier  : require

#define USE_BINDLESS
#include "input_structures.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec4  outAccum;
layout(location = 1) out float outReveal;

float wboit_weight(float z, float a) {
    return clamp(
        pow(min(1.0, a * 10.0) + 0.01, 3.0) * 1e8 * pow(1.0 - z * 0.9, 3.0),
        1e-2, 3e3);
}

void main() {
    float lightValue = max(dot(inNormal, vec3(0.3, 1.0, 0.3)), 0.1);
    vec4 texColor    = texture(allTextures[materialData.colorTexID], inUV);
    vec3 color       = inColor * texColor.rgb * lightValue;
    float alpha      = texColor.a * materialData.colorFactors.a;

    float w = wboit_weight(gl_FragCoord.z, alpha);
    outAccum  = vec4(color * alpha * w, alpha * w);
    outReveal = alpha;
}
