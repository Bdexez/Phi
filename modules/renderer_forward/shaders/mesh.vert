#version 450
#extension GL_GOOGLE_include_directive : require
#include "bdex/frame_uniforms.glsl"

// Per-object data, one entry per instance. Mirrors ObjectData in MeshFeature.hpp.
struct ObjectData {
    mat4 model;
    vec4 baseColor;
    vec4 params;    // x: specular strength, y: shininess, z: normal map strength
    vec4 emissive;  // rgb
};

layout(std430, set = 2, binding = 0) readonly buffer Objects {
    ObjectData objects[];
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inTangent;  // xyz: +U direction, w: bitangent sign

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec3 outWorldNormal;
layout(location = 2) out vec2 outUv;
layout(location = 3) flat out vec4 outBaseColor;
layout(location = 4) flat out vec4 outParams;
layout(location = 5) flat out vec3 outEmissive;
layout(location = 6) out vec4 outWorldTangent;  // w: bitangent sign

void main() {
    // gl_InstanceIndex includes the draw's first instance.
    ObjectData object = objects[gl_InstanceIndex];

    vec4 worldPosition = object.model * vec4(inPosition, 1.0);
    outWorldPosition = worldPosition.xyz;
    // Normals transform by the inverse-transpose of the model matrix, which
    // keeps them perpendicular under non-uniform scale. The cofactor matrix
    // equals it times the determinant: three cross products instead of a
    // 4x4 inverse per vertex. The fragment shader normalizes, so only the
    // determinant's sign (mirroring) has to be applied.
    mat3 linear = mat3(object.model);
    mat3 cofactor = mat3(cross(linear[1], linear[2]),
                         cross(linear[2], linear[0]),
                         cross(linear[0], linear[1]));
    float handedness = dot(linear[0], cofactor[0]) < 0.0 ? -1.0 : 1.0;
    outWorldNormal = cofactor * inNormal * handedness;
    // Tangents lie in the surface: they transform like positions. Mirroring
    // flips the bitangent.
    outWorldTangent = vec4(linear * inTangent.xyz, inTangent.w * handedness);
    outUv = inUv;
    outBaseColor = object.baseColor;
    outParams = object.params;
    outEmissive = object.emissive.rgb;
    gl_Position = frame.viewProjection * worldPosition;
}
