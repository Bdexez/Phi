// Sun and spot light shadows, for shaders that include frame_uniforms.glsl.
//
// Usage:
//   #include "bdex/frame_uniforms.glsl"
//   #include "bdex/shadows.glsl"
//   float lit = bdexSunShadow(worldPosition, normalize(worldNormal));
//   float spotLit = bdexSpotShadow(spotIndex, worldPosition, normalize(worldNormal));

#ifndef BDEX_SHADOWS_GLSL
#define BDEX_SHADOWS_GLSL

// Filtered comparison in one cell of the atlas (3x3 taps, each bilinearly
// filtered by the sampler): 1 lit, 0 in full shadow.
float bdexAtlasShadow(int cell, vec2 uv, float reference) {
    vec2 texel = frame.shadowParams.yz;
    vec2 size = vec2(0.25, 0.5);
    vec2 corner = vec2(cell % 4, cell / 4) * size;
    // Keep the filter taps inside the cell.
    vec2 atlas = clamp(corner + uv * size, corner + texel * 1.5, corner + size - texel * 1.5);
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            sum += texture(bdexShadowMap, vec3(atlas + vec2(x, y) * texel, reference));
        }
    }
    return sum / 9.0;
}

// Shadow from one cascade: 1 where the sun reaches `position`, 0 in full
// shadow; 1 outside the cascade's volume.
float bdexCascadeShadow(int cascade, vec3 position, vec3 normal) {
    // Offsetting along the normal keeps surfaces from shadowing themselves.
    vec4 bias = frame.cascadeBiases[cascade];
    vec4 clip = frame.shadowCascades[cascade] * vec4(position + normal * bias.y, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0) {
        return 1.0;
    }
    return bdexAtlasShadow(cascade, uv, ndc.z - bias.x);
}

// Shadow of spot light `spot` at `position`: 1 lit (or no shadow for this
// light), 0 in full shadow.
float bdexSpotShadow(int spot, vec3 position, vec3 normal) {
    vec4 params = frame.spotShadowParams[spot];
    if (params.x < 0.0) {
        return 1.0;
    }
    int index = int(params.x);
    // Texels grow with the distance to the light, and so does the offset.
    float distance = length(frame.spotLightPositions[spot].xyz - position);
    vec4 clip = frame.spotShadows[index] * vec4(position + normal * (params.z * distance), 1.0);
    if (clip.w <= 0.0) {
        return 1.0;
    }
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0) {
        return 1.0;
    }
    return bdexAtlasShadow(BDEX_SHADOW_CASCADES + index, uv, ndc.z - params.y);
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
