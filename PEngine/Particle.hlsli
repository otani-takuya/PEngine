
#ifndef PARTICLE_HLSLI
#define PARTICLE_HLSLI

// ========================================
// Vertex Shaderの入力データ
// ========================================
struct VertexShaderInput
{
    // 頂点座標
    float4 position : POSITION;

    // UV座標
    float2 texcoord : TEXCOORD0;

    // 法線
    float3 normal : NORMAL0;
};

// ========================================
// Vertex Shaderの出力データ
// Pixel Shaderの入力としても使用
// ========================================
struct VertexShaderOutput
{
    // 変換後の頂点座標
    float4 position : SV_POSITION;

    // UV座標
    float2 texcoord : TEXCOORD0;

    // 法線
    float3 normal : NORMAL0;

    // Particleごとの色（RGBA）
    float4 color : COLOR0;
};

#endif
