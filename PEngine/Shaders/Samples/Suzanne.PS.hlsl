struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL0;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

cbuffer Material : register(b0)
{
    float4 color;
    int lightingType;
    float3 padding;
    float4x4 uvTransform;
};

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

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;

    float3 normal = normalize(input.normal);
    float3 lightDirection =
        normalize(-gDirectionalLight.direction);

    // Lightingなし
    if (lightingType == 0)
    {
        output.color = color;
    }

    // Lambert
    else if (lightingType == 1)
    {
        float diffuse =
            saturate(
                dot(normal, lightDirection)
            );

        output.color =
            color *
            gDirectionalLight.color *
            diffuse *
            gDirectionalLight.intensity;
    }

    // Half Lambert
    else
    {
        float halfLambert =
            dot(normal, lightDirection) * 0.5f +
            0.5f;

        halfLambert *= halfLambert;

        output.color =
            color *
            gDirectionalLight.color *
            halfLambert *
            gDirectionalLight.intensity;
    }

    output.color.a = color.a;

    return output;
}