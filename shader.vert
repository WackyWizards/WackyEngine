#version 450

// Match C++ struct layout exactly, no vec2/vec3 to avoid std430 padding surprises.
// C++ Sprite: float x, y, halfW, halfH, r, g, b = 7 floats = 28 bytes, tightly packed.
layout(push_constant) uniform PC {
    float data[4 * 7 + 1]; // 4 sprites * 7 floats + 1 uint count (as float slot)
} pc;

layout(location = 0) out vec3 fragColor;

const vec2 OFFSETS[6] = vec2[](
    vec2(-1, +1), vec2(+1, +1), vec2(+1, -1),
    vec2(-1, +1), vec2(+1, -1), vec2(-1, -1)
);

void main()
{
    int si = gl_VertexIndex / 6;
    int vi = gl_VertexIndex % 6;

    int base = si * 7; // 7 floats per sprite
    float x = pc.data[base + 0];
    float y = pc.data[base + 1];
    float halfW = pc.data[base + 2];
    float halfH = pc.data[base + 3];
    float r = pc.data[base + 4];
    float g = pc.data[base + 5];
    float b = pc.data[base + 6];

    vec2 pos = vec2(x, y) + OFFSETS[vi] * vec2(halfW, halfH);

    gl_Position = vec4(pos, 0.0, 1.0);
    fragColor = vec3(r, g, b);
}
