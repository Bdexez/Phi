// Directional light shadows, for shaders that include frame_uniforms.glsl.
//
// Usage:
//   #include "bdex/frame_uniforms.glsl"
//   #include "bdex/shadows.glsl"
//   float lit = bdexSunShadow(worldPosition, normalize(worldNormal));

#ifndef BDEX_SHADOWS_GLSL
#define BDEX_SHADOWS_GLSL

// 1 where the sun reaches `position`, 0 in full shadow, filtered in between
// (3x3 comparisons, each bilinearly filtered by the sampler).
float bdexSunShadow(vec3 position, vec3 normal) {
    if (frame.shadowParams.x == 0.0) {
        return 1.0;
    }
    // Offsetting along the normal keeps surfaces from shadowing themselves.
    vec4 clip = frame.lightViewProjection * vec4(position + normal * frame.shadowParams.w, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0) {
        return 1.0;  // outside the shadowed area
    }
    float reference = ndc.z - frame.shadowParams.z;
    float texel = frame.shadowParams.y;
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            sum += texture(bdexShadowMap, vec3(uv + vec2(x, y) * texel, reference));
        }
    }
    return sum / 9.0;
}

#endif
