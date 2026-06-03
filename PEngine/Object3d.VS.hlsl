#include "object3d.hlsli"

cbuffer TransformationMatrix : register(b0)
{
    float4x4 WVP;
};

struct VertexShaderInput
{
    float4 position : POSITION;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    output.position = mul(input.position, WVP);
    output.texcoord = input.position.xy;

    return output;
}