#version 450
#extension GL_GOOGLE_include_directive : require
#include "bdex/frame_uniforms.glsl"
#include "bdex/shadows.glsl"

layout(set = 1, binding = 0) uniform sampler2D albedoTexture;
layout(set = 1, binding = 1) uniform sampler2D normalTexture;  // tangent space, Y up

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) flat in vec4 inBaseColor;
layout(location = 4) flat in vec4 inParams;  // x: specular, y: shininess, z: normal map strength
layout(location = 5) flat in vec3 inEmissive;
layout(location = 6) in vec4 inWorldTangent;  // w: bitangent sign

layout(location = 0) out vec4 outColor;

// Blinn-Phong response to one light, in linear space.
vec3 shade(vec3 albedo, vec3 normal, vec3 toCamera, vec3 toLight, vec3 radiance) {
    float diffuse = max(dot(normal, toLight), 0.0);
    if (diffuse <= 0.0) {
        return vec3(0.0);
    }
    vec3 halfVector = normalize(toLight + toCamera);
    float specular = pow(max(dot(normal, halfVector), 0.0), inParams.y) * inParams.x;
    return radiance * (albedo * diffuse + specular);
}

void main() {
    vec4 albedo = texture(albedoTexture, inUv) * inBaseColor;
    // Tangent-space normal map, applied on the interpolated surface frame.
    vec3 geometric = normalize(inWorldNormal);
    vec3 tangent = inWorldTangent.xyz - geometric * dot(geometric, inWorldTangent.xyz);
    vec3 bumped = texture(normalTexture, inUv).xyz * 2.0 - 1.0;
    bumped.xy *= inParams.z;
    vec3 normal = geometric;
    if (dot(tangent, tangent) > 1e-12) {
        tangent = normalize(tangent);
        vec3 bitangent = cross(geometric, tangent) * inWorldTangent.w;
        normal = normalize(tangent * bumped.x + bitangent * bumped.y + geometric * bumped.z);
    }
    vec3 toCamera = normalize(frame.cameraPosition.xyz - inWorldPosition);

    // The render target is sRGB-encoded: lighting is computed linearly.
    vec3 color = albedo.rgb * frame.ambientColor.rgb;
    color += shade(albedo.rgb, normal, toCamera, normalize(-frame.lightDirection.xyz),
                   frame.lightColor.rgb * bdexSunShadow(inWorldPosition, geometric));

    int pointLights = int(frame.pointLightCount.x);
    for (int i = 0; i < pointLights; ++i) {
        vec3 toLight = frame.pointLightPositions[i].xyz - inWorldPosition;
        float range = frame.pointLightPositions[i].w;
        float distanceSquared = dot(toLight, toLight);
        // Inverse-square falloff, windowed so it reaches zero at the range.
        float ratio = distanceSquared / (range * range);
        float window = clamp(1.0 - ratio * ratio, 0.0, 1.0);
        float attenuation = window * window / max(distanceSquared, 1e-4);
        if (attenuation > 0.0) {
            color += shade(albedo.rgb, normal, toCamera, toLight * inversesqrt(distanceSquared),
                           frame.pointLightColors[i].rgb * attenuation);
        }
    }
    outColor = vec4(color + inEmissive, albedo.a);
}
