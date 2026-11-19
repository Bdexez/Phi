#version 450

// One step down the bloom chain: a 13-tap filter (Jimenez, "Next Generation
// Post Processing in Call of Duty: Advanced Warfare") that halves the
// resolution without the flicker of a plain box filter.
layout(push_constant) uniform Constants {
    vec4 sizes;  // xy: source texel size (UV), zw: target size (pixels)
} constants;

layout(set = 0, binding = 0) uniform sampler2D source;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = gl_FragCoord.xy / constants.sizes.zw;
    vec2 t = constants.sizes.xy;
    vec3 a = texture(source, uv + t * vec2(-2.0, -2.0)).rgb;
    vec3 b = texture(source, uv + t * vec2(0.0, -2.0)).rgb;
    vec3 c = texture(source, uv + t * vec2(2.0, -2.0)).rgb;
    vec3 d = texture(source, uv + t * vec2(-2.0, 0.0)).rgb;
    vec3 e = texture(source, uv).rgb;
    vec3 f = texture(source, uv + t * vec2(2.0, 0.0)).rgb;
    vec3 g = texture(source, uv + t * vec2(-2.0, 2.0)).rgb;
    vec3 h = texture(source, uv + t * vec2(0.0, 2.0)).rgb;
    vec3 i = texture(source, uv + t * vec2(2.0, 2.0)).rgb;
    vec3 j = texture(source, uv + t * vec2(-1.0, -1.0)).rgb;
    vec3 k = texture(source, uv + t * vec2(1.0, -1.0)).rgb;
    vec3 l = texture(source, uv + t * vec2(-1.0, 1.0)).rgb;
    vec3 m = texture(source, uv + t * vec2(1.0, 1.0)).rgb;
    vec3 color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 +
                 (j + k + l + m) * 0.125;
    outColor = vec4(max(color, vec3(0.0)), 1.0);
}
