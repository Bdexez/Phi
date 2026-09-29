// Directional light shadows, for shaders that include frame_uniforms.glsl.
//
// Usage:
//   #include "bdex/frame_uniforms.glsl"
//   #include "bdex/shadows.glsl"
//   float lit = bdexSunShadow(worldPosition, normalize(worldNormal));

#ifndef BDEX_SHADOWS_GLSL
#define BDEX_SHADOWS_GLSL

// Shadow from one cascade: 1 where the sun reaches `position`, 0 in full
// shadow, filtered in between (3x3 comparisons, each bilinearly filtered by
// the sampler); 1 outside the cascade's volume.
float bdexCascadeShadow(int cascade, vec3 position, vec3 normal) {
    // Offsetting along the normal keeps surfaces from shadowing themselves.
    vec4 bias = frame.cascadeBiases[cascade];
    vec4 clip = frame.shadowCascades[cascade] * vec4(position + normal * bias.y, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0) {
        return 1.0;
    }

    // Into the cascade's cell of the 2 x 2 atlas, keeping the filter taps
    // inside it.
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

// 1 where the sun reaches `position`, 0 in full shadow. The nearest cascade
// covering the point is used; over the last tenth of a cascade's range it
// blends into the next one, so the change of resolution leaves no seam, and
// the last one fades out toward the shadow distance instead of ending in a
// line.
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

    float shadow = bdexCascadeShadow(cascade, position, normal);
    float near = cascade == 0 ? 0.0 : frame.cascadeSplits[cascade - 1];
    float far = frame.cascadeSplits[cascade];
    float band = (far - near) * 0.1;
    float blend = clamp((viewDepth - (far - band)) / max(band, 1e-4), 0.0, 1.0);
    if (blend > 0.0) {
        float next = cascade + 1 < BDEX_SHADOW_CASCADES ? bdexCascadeShadow(cascade + 1, position, normal) : 1.0;
        shadow = mix(shadow, next, blend);
    }
    return shadow;
}

#endif
