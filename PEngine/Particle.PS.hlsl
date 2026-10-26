
#include "Particle.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

cbuffer Material : register(b0)
{
    float4 color;
    int lightingType;
    float3 padding;
    float4x4 uvTransform;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 transformedUV = mul(
        float4(input.texcoord, 0.0f, 1.0f),
        uvTransform
    );

    float4 textureColor = gTexture.Sample(
        gSampler,
        transformedUV.xy
    );

    output.color = color * textureColor;

    if (output.color.a == 0.0f)
    {
        discard;
    }

    return output;
}
