[[vk::push_constant]]
cbuffer PC
{
    float data[29]; // [sprite * 7 + field], count packed as float in slot 28
};

// Two triangles per quad, 6 vertices each.
// Corner offsets: BL BR TR | BL TR TL
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
    int si   = (int)vertexID / 6; // which sprite
    int vi   = (int)vertexID % 6; // which corner
    int base = si * 7;

    float x = data[base + 0];
    float y = data[base + 1];
    float halfW = data[base + 2];
    float halfH = data[base + 3];
    float r = data[base + 4];
    float g = data[base + 5];
    float b = data[base + 6];

    float2 pos = float2(x, y) + OFFSETS[vi] * float2(halfW, halfH);

    VSOutput o;
    o.pos = float4(pos, 0.0, 1.0);
    o.color = float3(r, g, b);
    return o;
}

[shader("fragment")]
float4 PSMain(VSOutput i) : SV_Target
{
    return float4(i.color, 1.0);
}
