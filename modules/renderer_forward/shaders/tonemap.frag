#version 450

// Maps the HDR scene color to the display range. Mirrors TonemapConstants in
// ForwardRenderer.cpp.
layout(push_constant) uniform TonemapConstants {
    vec4 settings;  // x: exposure, y: operator (0: clamp, 1: ACES)
} tonemap;

layout(set = 0, binding = 0) uniform sampler2D hdrScene;

layout(location = 0) out vec4 outColor;

// ACES filmic curve, Narkowicz's fit: gentle toe, highlights roll off to
// white instead of clipping.
vec3 aces(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    // Same size as the output: one texel per pixel, no filtering.
    vec4 hdr = texelFetch(hdrScene, ivec2(gl_FragCoord.xy), 0);
    vec3 color = hdr.rgb * tonemap.settings.x;
    if (tonemap.settings.y > 0.5) {
        color = aces(color);
    }
    // The output target is sRGB: the hardware encodes the linear result.
    outColor = vec4(clamp(color, 0.0, 1.0), clamp(hdr.a, 0.0, 1.0));
}
