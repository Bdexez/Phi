#version 450
#extension GL_GOOGLE_include_directive : require
#include "bdex/frame_uniforms.glsl"

// Particles as camera-facing quads, expanded on the CPU.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;

void main() {
    outUv = inUv;
    outColor = inColor;
    gl_Position = frame.viewProjection * vec4(inPosition, 1.0);
}
