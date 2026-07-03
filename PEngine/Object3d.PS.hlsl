#include "object3d.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

cbuffer Material : register(b0)
{
    float4 color;
    int enableLighting;
    float padding[3];
    float4x4 uvTransform;
};


Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct DirectionalLight
{
    float4 color; //ライトの色
    float3 direction; //ライトの向き
    float intensity; //輝度
};

cbuffer DirectionalLightBuffer : register(b1)
{
    DirectionalLight gDirectionalLight;
};


PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;

    float4 textureColor =
        gTexture.Sample(gSampler, input.texcoord);

    if (enableLighting != 0)
    {
        //half lambert
        float NdotL = dot(
                    normalize(input.normal),
                    -gDirectionalLight.direction
                );
           
        float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);


        output.color =
            color *
            textureColor *
            gDirectionalLight.color *
            cos *
            gDirectionalLight.intensity;
    }
    else
    {
        output.color =
            color *
            textureColor;
    }

    return output;
}
