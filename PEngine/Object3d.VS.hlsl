#include "object3d.hlsli"

cbuffer TransformationMatrix : register(b0)
{
    float4x4 WVP;
    float4x4 World;
};

struct VertexShaderInput
{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    output.position = mul(input.position, WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float3x3) World));

    return output;
}
