// Sprite data is uploaded each frame into a per-frame storage buffer.
// The draw call passes exactly scene.count * 6 vertices, so no count
// field is needed in the shader itself.
struct Sprite
{
    float x, y;
    float halfW, halfH;
    float r, g, b;
};

[[vk::binding(0, 0)]]
StructuredBuffer<Sprite> sprites;

// Two triangles per quad, 6 vertices each.
// Corner offsets: TL TR BR | TL BR BL
static const float2 OFFSETS[6] =
{
    float2(-1, +1), float2(+1, +1), float2(+1, -1),
    float2(-1, +1), float2(+1, -1), float2(-1, -1)
};

struct VSOutput
{
    float4 pos   : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
VSOutput VSMain(uint vertexID : SV_VertexID)
{
    int si = (int)vertexID / 6; // which sprite
    int vi = (int)vertexID % 6; // which corner

    Sprite s = sprites[si];
    float2 pos = float2(s.x, s.y) + OFFSETS[vi] * float2(s.halfW, s.halfH);

    VSOutput o;
    o.pos   = float4(pos, 0.0, 1.0);
    o.color = float3(s.r, s.g, s.b);
    return o;
}

[shader("fragment")]
float4 PSMain(VSOutput i) : SV_Target
{
    return float4(i.color, 1.0);
}
