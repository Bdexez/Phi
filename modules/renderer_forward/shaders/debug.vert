#version 450
#extension GL_GOOGLE_include_directive : require
#include "phi/frame_uniforms.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = inColor;
    gl_Position = frame.viewProjection * vec4(inPosition, 1.0);
}
