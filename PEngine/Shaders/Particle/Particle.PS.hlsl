
#include "Particle.hlsli"

// ========================================
// Pixel Shaderの出力データ
// ========================================
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

// ========================================
// Material設定
// ========================================
cbuffer Material : register(b0)
{
    // Material全体の色
    float4 color;

    // ライティング設定
    int lightingType;

    // アライメント用
    float3 padding;

    // UV変換行列
    float4x4 uvTransform;
};

// ========================================
// Texture / Sampler
// ========================================
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// ========================================
// Pixel Shader
// ========================================
PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // ========================================
    // UV座標を変換
    // ========================================
    float4 transformedUV = mul(
        float4(input.texcoord, 0.0f, 1.0f),
        uvTransform
    );

    // ========================================
    // Textureの色を取得
    // ========================================
    float4 textureColor = gTexture.Sample(
        gSampler,
        transformedUV.xy
    );

    // ========================================
    // 最終的な描画色を計算
    // ========================================

    // Material色 × Texture色 × Particle色
    output.color =
        color *
        textureColor *
        input.color;

    // ========================================
    // 完全透明なPixelを破棄
    // ========================================
    if (output.color.a <= 0.0f)
    {
        discard;
    }

    return output;
}
