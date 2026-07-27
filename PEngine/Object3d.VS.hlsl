#include "Object3d.hlsli"

// ==============================
// 座標変換行列
// ==============================
cbuffer TransformationMatrix : register(b1)
{
    float4x4 WVP;
    float4x4 World;
};

// ==============================
// Vertex Shaderへの入力
// ==============================
struct VertexShaderInput
{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

// ==============================
// Vertex Shader
// ==============================
VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    // 頂点をクリップ座標へ変換
    output.position = mul(input.position, WVP);

    // UVTransformはPixel Shader側で行うため、
    // 元のUV座標をそのまま渡す
    output.texcoord = input.texcoord;

    // 法線をワールド空間へ変換
    output.normal =
        normalize(mul(input.normal, (float3x3) World));

    return output;
}