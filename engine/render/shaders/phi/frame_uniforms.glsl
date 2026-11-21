// Per-frame constants provided by every phi renderer as set 0, binding 0.
// Mirrors phi::render::FrameUniforms (engine/render/include/phi/render/Renderer.hpp).
//
// Usage:
//   #extension GL_GOOGLE_include_directive : require
//   #include "phi/frame_uniforms.glsl"

#ifndef PHI_FRAME_UNIFORMS_GLSL
#define PHI_FRAME_UNIFORMS_GLSL

#define PHI_MAX_POINT_LIGHTS 16
#define PHI_SHADOW_CASCADES 4
#define PHI_MAX_SPOT_LIGHTS 8
#define PHI_SHADOWED_SPOT_LIGHTS 4

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 cameraPosition;  // xyz
    vec4 lightDirection;  // xyz, normalized, direction the light travels in
    vec4 lightColor;      // rgb, premultiplied by intensity
    vec4 ambientColor;    // rgb
    vec4 timeAndExtent;   // elapsed seconds, delta seconds, width, height
    mat4 shadowCascades[PHI_SHADOW_CASCADES];  // world to each cascade's clip space
    vec4 cascadeSplits;  // far view distance of each cascade
    vec4 cascadeBiases[PHI_SHADOW_CASCADES];  // x: depth bias, y: normal offset
    vec4 shadowParams;  // x: sun shadows on, y / z: atlas texel size in U / V
    vec4 pointLightCount;  // x: point lights below, y: spot lights
    vec4 pointLightPositions[PHI_MAX_POINT_LIGHTS];  // xyz: world position, w: range
    vec4 pointLightColors[PHI_MAX_POINT_LIGHTS];     // rgb, premultiplied by intensity
    vec4 spotLightPositions[PHI_MAX_SPOT_LIGHTS];    // xyz: world position, w: range
    vec4 spotLightDirections[PHI_MAX_SPOT_LIGHTS];   // xyz: unit axis, w: cos(outer angle)
    vec4 spotLightColors[PHI_MAX_SPOT_LIGHTS];       // rgb premultiplied, w: cos(inner angle)
    mat4 spotShadows[PHI_SHADOWED_SPOT_LIGHTS];      // world to each spot shadow's clip space
    vec4 spotShadowParams[PHI_MAX_SPOT_LIGHTS];      // x: shadow index or -1, y: depth bias, z: normal offset / m
} frame;

// World to clip space of a shadow view: the sun's cascades, then spot
// lights' shadows.
mat4 phiShadowView(uint view) {
    return view < PHI_SHADOW_CASCADES ? frame.shadowCascades[view] : frame.spotShadows[view - PHI_SHADOW_CASCADES];
}

// Depth of the shadow casters, read with comparison (see phi/shadows.glsl):
// a 4 x 2 atlas, the sun's cascades in the first row, spot lights' in the
// second.
layout(set = 0, binding = 1) uniform sampler2DShadow phiShadowMap;

#endif
