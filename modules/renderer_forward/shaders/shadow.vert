#version 450
#extension GL_GOOGLE_include_directive : require
#include "phi/frame_uniforms.glsl"

// Depth-only shadow pass: same object buffer as mesh.vert.
struct ObjectData {
    mat4 model;
    vec4 baseColor;
    vec4 params;
    vec4 emissive;
};

layout(std430, set = 2, binding = 0) readonly buffer Objects {
    ObjectData objects[];
};

layout(push_constant) uniform ShadowConstants {
    uvec4 cascade;  // x: the shadow view this pass renders (see phiShadowView)
} shadow;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = phiShadowView(shadow.cascade.x) * objects[gl_InstanceIndex].model * vec4(inPosition, 1.0);
}
