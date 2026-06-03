#include "object3d.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

cbuffer Material : register(b0)
{
    float4 color;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;

    float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    output.color = color * textureColor;

    return output;
}