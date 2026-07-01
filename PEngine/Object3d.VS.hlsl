#include "object3d.hlsli"

cbuffer TransformationMatrix : register(b1)
{
    float4x4 WVP;
    float4x4 World;
};

cbuffer Material : register(b0)
{
    float4 color;
    int enableLighting;
    float4x4 uvTransform;
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
    
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), uvTransform);

    output.texcoord = transformedUV.xy;
    
    output.normal = normalize(mul(input.normal, (float3x3) World));

    return output;
}
