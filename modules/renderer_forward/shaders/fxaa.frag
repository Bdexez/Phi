#version 450

// Fast approximate anti-aliasing (after Timothy Lottes' FXAA): finds edges
// from luma contrast with the 4 diagonal neighbours, then blends along the
// edge direction. Runs on the tone mapped image.
layout(push_constant) uniform Constants {
    vec4 texel;  // xy: one texel in UV
} constants;

layout(set = 0, binding = 0) uniform sampler2D image;

layout(location = 0) out vec4 outColor;

// Perceptual luma: the input is linear (sampled from an sRGB image).
float luma(vec3 color) {
    return sqrt(dot(color, vec3(0.299, 0.587, 0.114)));
}

void main() {
    vec2 uv = gl_FragCoord.xy * constants.texel.xy;
    vec4 center = texture(image, uv);
    float lumaM = luma(center.rgb);
    float lumaNW = luma(textureOffset(image, uv, ivec2(-1, -1)).rgb);
    float lumaNE = luma(textureOffset(image, uv, ivec2(1, -1)).rgb);
    float lumaSW = luma(textureOffset(image, uv, ivec2(-1, 1)).rgb);
    float lumaSE = luma(textureOffset(image, uv, ivec2(1, 1)).rgb);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    if (lumaMax - lumaMin < max(0.0312, lumaMax * 0.125)) {
        outColor = center;  // no edge here
        return;
    }

    // Edge direction, scaled so the blur reaches up to 8 texels along it.
    vec2 direction = vec2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)), (lumaNW + lumaSW) - (lumaNE + lumaSE));
    float reduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125, 1.0 / 128.0);
    float scale = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduce);
    direction = clamp(direction * scale, vec2(-8.0), vec2(8.0)) * constants.texel.xy;

    vec3 inner = 0.5 * (texture(image, uv + direction * (1.0 / 3.0 - 0.5)).rgb +
                        texture(image, uv + direction * (2.0 / 3.0 - 0.5)).rgb);
    vec3 outer = inner * 0.5 + 0.25 * (texture(image, uv - direction * 0.5).rgb +
                                       texture(image, uv + direction * 0.5).rgb);
    // The wider blur may cross into another edge: keep the narrow one then.
    float lumaOuter = luma(outer);
    outColor = vec4(lumaOuter < lumaMin || lumaOuter > lumaMax ? inner : outer, center.a);
}
