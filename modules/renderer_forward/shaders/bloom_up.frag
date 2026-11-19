#version 450

// One step up the bloom chain: a 3x3 tent filter of the smaller level,
// added (by blending) onto the larger one.
layout(push_constant) uniform Constants {
    vec4 sizes;  // xy: source texel size (UV), zw: target size (pixels)
} constants;

layout(set = 0, binding = 0) uniform sampler2D source;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = gl_FragCoord.xy / constants.sizes.zw;
    vec2 t = constants.sizes.xy;
    vec3 color = texture(source, uv).rgb * 4.0;
    color += (texture(source, uv + t * vec2(-1.0, 0.0)).rgb + texture(source, uv + t * vec2(1.0, 0.0)).rgb +
              texture(source, uv + t * vec2(0.0, -1.0)).rgb + texture(source, uv + t * vec2(0.0, 1.0)).rgb) * 2.0;
    color += texture(source, uv + t * vec2(-1.0, -1.0)).rgb + texture(source, uv + t * vec2(1.0, -1.0)).rgb +
             texture(source, uv + t * vec2(-1.0, 1.0)).rgb + texture(source, uv + t * vec2(1.0, 1.0)).rgb;
    outColor = vec4(color / 16.0, 1.0);
}
