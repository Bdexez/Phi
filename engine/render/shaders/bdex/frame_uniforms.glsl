// Per-frame constants provided by every bdex renderer as set 0, binding 0.
// Mirrors bdex::render::FrameUniforms (engine/render/include/bdex/render/Renderer.hpp).
//
// Usage:
//   #extension GL_GOOGLE_include_directive : require
//   #include "bdex/frame_uniforms.glsl"

#ifndef BDEX_FRAME_UNIFORMS_GLSL
#define BDEX_FRAME_UNIFORMS_GLSL

#define BDEX_MAX_POINT_LIGHTS 16

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
    vec4 pointLightCount;  // x: number of valid entries below
    vec4 pointLightPositions[BDEX_MAX_POINT_LIGHTS];  // xyz: world position, w: range
    vec4 pointLightColors[BDEX_MAX_POINT_LIGHTS];     // rgb, premultiplied by intensity
} frame;

#endif
