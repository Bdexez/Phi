// Per-frame constants provided by every bdex renderer as set 0, binding 0.
// Mirrors bdex::render::FrameUniforms (engine/render/include/bdex/render/Renderer.hpp).
//
// Usage:
//   #extension GL_GOOGLE_include_directive : require
//   #include "bdex/frame_uniforms.glsl"

#ifndef BDEX_FRAME_UNIFORMS_GLSL
#define BDEX_FRAME_UNIFORMS_GLSL

#define BDEX_MAX_POINT_LIGHTS 16
#define BDEX_SHADOW_CASCADES 4
#define BDEX_MAX_SPOT_LIGHTS 8

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
    mat4 shadowCascades[BDEX_SHADOW_CASCADES];  // world to each cascade's clip space
    vec4 cascadeSplits;  // far view distance of each cascade
    vec4 cascadeBiases[BDEX_SHADOW_CASCADES];  // x: depth bias, y: normal offset
    vec4 shadowParams;  // x: shadows on, y: atlas texel size (UV)
    vec4 pointLightCount;  // x: point lights below, y: spot lights
    vec4 pointLightPositions[BDEX_MAX_POINT_LIGHTS];  // xyz: world position, w: range
    vec4 pointLightColors[BDEX_MAX_POINT_LIGHTS];     // rgb, premultiplied by intensity
    vec4 spotLightPositions[BDEX_MAX_SPOT_LIGHTS];    // xyz: world position, w: range
    vec4 spotLightDirections[BDEX_MAX_SPOT_LIGHTS];   // xyz: unit axis, w: cos(outer angle)
    vec4 spotLightColors[BDEX_MAX_SPOT_LIGHTS];       // rgb premultiplied, w: cos(inner angle)
} frame;

// Depth of the directional light's shadow casters, read with comparison
// (see bdex/shadows.glsl).
layout(set = 0, binding = 1) uniform sampler2DShadow bdexShadowMap;

#endif
