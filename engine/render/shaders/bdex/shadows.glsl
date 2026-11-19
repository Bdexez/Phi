// Directional light shadows, for shaders that include frame_uniforms.glsl.
//
// Usage:
//   #include "bdex/frame_uniforms.glsl"
//   #include "bdex/shadows.glsl"
//   float lit = bdexSunShadow(worldPosition, normalize(worldNormal));

#ifndef BDEX_SHADOWS_GLSL
#define BDEX_SHADOWS_GLSL

// 1 where the sun reaches `position`, 0 in full shadow, filtered in between
// (3x3 comparisons, each bilinearly filtered by the sampler). Cascades sit
// in a 2 x 2 atlas; the nearest one covering the point is used.
float bdexSunShadow(vec3 position, vec3 normal) {
    if (frame.shadowParams.x == 0.0) {
        return 1.0;
    }
    float viewDepth = -(frame.view * vec4(position, 1.0)).z;
    int cascade = 0;
    while (cascade < BDEX_SHADOW_CASCADES && viewDepth > frame.cascadeSplits[cascade]) {
        ++cascade;
    }
    if (cascade == BDEX_SHADOW_CASCADES) {
        return 1.0;  // beyond the shadow distance
    }

    // Offsetting along the normal keeps surfaces from shadowing themselves.
    vec4 bias = frame.cascadeBiases[cascade];
    vec4 clip = frame.shadowCascades[cascade] * vec4(position + normal * bias.y, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0) {
        return 1.0;  // outside the cascade's volume
    }

    // Into the cascade's cell, keeping the filter taps inside it.
    float texel = frame.shadowParams.y;
    vec2 cell = vec2(cascade & 1, cascade >> 1) * 0.5;
    vec2 atlas = clamp(cell + uv * 0.5, cell + vec2(texel * 1.5), cell + vec2(0.5 - texel * 1.5));
    float reference = ndc.z - bias.x;
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            sum += texture(bdexShadowMap, vec3(atlas + vec2(x, y) * texel, reference));
        }
    }
    return sum / 9.0;
}

#endif
