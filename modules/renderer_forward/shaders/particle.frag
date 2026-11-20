#version 450

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Mode {
    vec4 mode;  // x: 1 for additive blending (premultiplied output), 0 for alpha
} blend;

void main() {
    // A soft disc: full in the middle, fading to nothing at the quad's edge.
    float distance = length(inUv * 2.0 - 1.0);
    // No discard (it would need demote-to-helper support): a fragment with
    // no alpha adds nothing in either blend mode.
    float alpha = inColor.a * (1.0 - smoothstep(0.35, 1.0, distance));
    outColor = blend.mode.x > 0.5 ? vec4(inColor.rgb * alpha, 0.0) : vec4(inColor.rgb, alpha);
}
