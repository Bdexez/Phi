#version 450

// Overlay lines: positions in pixels from the top-left corner of the target,
// which is where Vulkan's clip space puts (-1, -1).
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Overlay {
    vec2 extent;
} overlay;

void main() {
    outColor = inColor;
    // Pixel centers, so lines on whole coordinates land on one pixel row.
    gl_Position = vec4((inPosition.xy + 0.5) / overlay.extent * 2.0 - 1.0, 0.0, 1.0);
}
