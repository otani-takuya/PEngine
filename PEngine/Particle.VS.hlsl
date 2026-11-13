
#include "Particle.hlsli"

// ========================================
// Instancing用データ
// ========================================
// C++のParticleTransformationMatrixと一致させる
struct TransformationMatrix
{
    // World View Projection行列
    float4x4 WVP;

    // World行列
    float4x4 World;

    // Particleごとの色（RGBA）
    float4 Color;
};

// ========================================
// Instancing用StructuredBuffer
// ========================================
StructuredBuffer<TransformationMatrix>
    gTransformationMatrices : register(t0);

// ========================================
// Vertex Shader
// ========================================
VertexShaderOutput main(
    VertexShaderInput input,
    uint instanceID : SV_InstanceID
)
{
    VertexShaderOutput output;

    // インスタンス番号からデータを取得
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
    
    
    // ========================================
    // Particleごとの色情報
    // ========================================

    // StructuredBufferから取得した色を
    // Pixel Shaderへ渡す
    output.color = transform.Color;


    return output;
}
