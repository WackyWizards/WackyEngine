struct Sprite
{
    float x, y;
    float halfW, halfH;
    float r, g, b;
    float uvX, uvY, uvW, uvH;
    float textured;
};

struct CameraUniforms {
    float ndcCenterX, ndcCenterY;
    float scaleX, scaleY;
    float panX, panY;
};

[[vk::push_constant]]
CameraUniforms cam;

[[vk::binding(0, 0)]]
StructuredBuffer<Sprite> sprites;

[[vk::binding(1, 0)]]
Sampler2D spriteTexture;

static const float2 OFFSETS[6] =
{
    float2(-1, +1), float2(+1, +1), float2(+1, -1),
    float2(-1, +1), float2(+1, -1), float2(-1, -1)
};

static const float2 UV_CORNERS[6] =
{
    float2(0, 1), float2(1, 1), float2(1, 0),
    float2(0, 1), float2(1, 0), float2(0, 0)
};

struct VSOutput
{
    float4 pos : SV_Position;
    float3 color : COLOR0;
    float2 uv : TEXCOORD0;
    float textured : TEXCOORD1;
};

[shader("vertex")]
VSOutput VSMain(uint vertexID : SV_VertexID)
{
    int si = (int)vertexID / 6;
    int vi = (int)vertexID % 6;

    Sprite s = sprites[si];
    float2 worldPos = float2(s.x, s.y) + OFFSETS[vi] * float2(s.halfW, s.halfH);
    float ndcX = cam.ndcCenterX + (worldPos.x - cam.panX) * cam.scaleX;
    float ndcY = cam.ndcCenterY + (worldPos.y - cam.panY) * cam.scaleY;
    float2 uv = float2(s.uvX, s.uvY) + UV_CORNERS[vi] * float2(s.uvW, s.uvH);

    VSOutput o;
    o.pos = float4(ndcX, ndcY, 0.0, 1.0);
    o.color = float3(s.r, s.g, s.b);
    o.uv = uv;
    o.textured = s.textured;
    return o;
}

[shader("fragment")]
float4 PSMain(VSOutput i) : SV_Target
{
    if (i.textured > 0.5)
    {
        float4 texColor = spriteTexture.Sample(float2(i.uv.x, i.uv.y));
        if (texColor.a < 0.01)
        {
            discard;
        }
        return texColor;
    }

    return float4(i.color, 1.0);
}