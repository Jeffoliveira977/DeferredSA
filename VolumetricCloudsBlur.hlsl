#include "Utilities.hlsl"

sampler2D cloudsColorTex : register(s0);
sampler2D sceneDepthTex : register(s1);

static const float KERNEL_RADIUS = 16.0f;

float4 onepass_blur(float2 uv, uint2 resolution)
{
    float offset_x = 1.0f / (float) resolution.x;
    float offset_y = 1.0f / (float) resolution.y;
    
    float2 offsets[9] =
    {
        float2(-offset_x, offset_y),
        float2(0.0f, offset_y),
        float2(offset_x, offset_y),
        float2(-offset_x, 0.0f),
        float2(0.0f, 0.0f),
        float2(offset_x, 0.0f),
        float2(-offset_x, -offset_y),
        float2(0.0f, -offset_y),
        float2(offset_x, -offset_y)
    };

    float kernel[9] =
    {
        1.0f / KERNEL_RADIUS, 2.0f / KERNEL_RADIUS, 1.0 / KERNEL_RADIUS,
		2.0f / KERNEL_RADIUS, 4.0f / KERNEL_RADIUS, 2.0 / KERNEL_RADIUS,
		1.0f / KERNEL_RADIUS, 2.0f / KERNEL_RADIUS, 1.0 / KERNEL_RADIUS
    };
    
    float4 sampleTex[9];
    bool foundOccluder = false;
    for (int i = 0; i < 9; i++)
    {
        float4 pixel = tex2D(cloudsColorTex, uv + offsets[i]);
      //  float depth = sceneDepthTex.Sample(SimpleSampler, uv + offsets[i]).r;
        float depth;
        float3 normal;
        float3 worldPosition;
        DecodeDepth(sceneDepthTex,texCoord, FogData.y, depth, normal);
        if (depth < 0.999f)
        {
            foundOccluder = true;
            break;
        }
        
        sampleTex[i] = pixel;
    }
    
    if (foundOccluder)
        return cloudsColorTex.Sample(SimpleSampler, uv);
    
    float4 col = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (int j = 0; j < 9; j++)
        col += sampleTex[j] * kernel[j];
    
    col = float4(col.rgb, 1.0f);
    return col;
}

float4 main(float4 pos : SV_POSITION, float2 tex : TEX_COORD0) : SV_Target
{
    uint width, height;
    cloudsColorTex.GetDimensions(width, height);
    
    float depth = sceneDepthTex.Sample(SimpleSampler, tex).r;
    if (depth < 0.999f)
        return cloudsColorTex.Sample(SimpleSampler, tex);
    else
        return onepass_blur(tex, uint2(width, height));
}