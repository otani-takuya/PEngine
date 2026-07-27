#include "Object3d.hlsli"

// ==============================
// Pixel Shader出力
// ==============================
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

// ==============================
// Material
// ==============================
cbuffer Material : register(b0)
{
    float4 color;
    int lightingType;
    float3 padding;
    float4x4 uvTransform;
};

// ==============================
// 平行光源
// ==============================
struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};

cbuffer DirectionalLightBuffer : register(b1)
{
    DirectionalLight gDirectionalLight;
};

// ==============================
// Texture・Sampler
// ==============================
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// ==============================
// Pixel Shader
// ==============================
PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UVTransformを適用
    float4 transformedUV =
        mul(
            float4(input.texcoord, 0.0f, 1.0f),
            uvTransform
        );

    // Textureの色を取得
    float4 textureColor =
        gTexture.Sample(
            gSampler,
            transformedUV.xy
        );

    // Materialの色とTextureの色を合成
    float4 baseColor = color * textureColor;

    // 法線を正規化
    float3 normal = normalize(input.normal);

    // ライトの方向を正規化
    // DirectionalLight.directionは
    // 「光が進む方向」なので反転して使用する
    float3 lightDirection =
        -normalize(gDirectionalLight.direction);

    // 法線とライト方向の内積
    float NdotL =
        dot(normal, lightDirection);

    // 最終的なRGB
    float3 finalRGB = baseColor.rgb;

    // ==============================
    // Lightingなし
    // ==============================
    if (lightingType == 0)
    {
        finalRGB = baseColor.rgb;
    }

    // ==============================
    // Lambert
    // ==============================
    else if (lightingType == 1)
    {
        float diffuse =
            saturate(NdotL);

        finalRGB =
            baseColor.rgb *
            gDirectionalLight.color.rgb *
            diffuse *
            gDirectionalLight.intensity;
    }

    // ==============================
    // Half Lambert
    // ==============================
    else if (lightingType == 2)
    {
        float halfLambert =
            NdotL * 0.5f + 0.5f;

        halfLambert =
            saturate(halfLambert);

        // 陰影を少しはっきりさせる
        halfLambert *= halfLambert;

        finalRGB =
            baseColor.rgb *
            gDirectionalLight.color.rgb *
            halfLambert *
            gDirectionalLight.intensity;
    }

    // 不正な値の場合はLightingなし
    else
    {
        finalRGB = baseColor.rgb;
    }

    // AlphaにはLightingを掛けない
    output.color = float4(
        finalRGB,
        baseColor.a
    );

    return output;
}