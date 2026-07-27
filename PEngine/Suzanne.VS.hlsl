struct VertexShaderInput
{
    float4 position : POSITION0;
    float3 normal : NORMAL0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL0;
};

cbuffer TransformationMatrix : register(b1)
{
    float4x4 WVP;
    float4x4 World;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    output.position = mul(input.position, WVP);

    output.normal = normalize(
        mul(input.normal, (float3x3) World)
    );

    return output;
}