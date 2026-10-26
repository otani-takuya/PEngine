
#include "Particle.hlsli"

// Instancing用のTransform情報
struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
};

// StructuredBuffer
StructuredBuffer<TransformationMatrix>
    gTransformationMatrices : register(t0);

// VertexShader
VertexShaderOutput main(
    VertexShaderInput input,
    uint instanceID : SV_InstanceID
)
{
    VertexShaderOutput output;

    // インスタンス番号からTransformを取得
    TransformationMatrix transform =
        gTransformationMatrices[instanceID];

    // 座標変換
    output.position = mul(
        input.position,
        transform.WVP
    );

    // UV座標
    output.texcoord = input.texcoord;

    // 法線変換
    output.normal = normalize(
        mul(input.normal, (float3x3) transform.World)
    );

    return output;
}
