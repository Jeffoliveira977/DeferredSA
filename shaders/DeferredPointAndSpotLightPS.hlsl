#include "Utilities.hlsl"
#include "Shadow.hlsl"

#define PI 3.14
#define USE_ADDITIONALGBUFFER	
#define AMBIENT_TERM_TYPE 1

row_major float4x4 InverseViewMatrix : register(c0);
row_major float4x4 ProjectionMatrix : register(c4);
float FarClip: register(c8);

struct LightData{
    float4 position;  // Position xyz, Range w
    float4 direction; // Direction xyz, Light type w
    float4 color;
};

LightData lightData : register(c9);

#define LightPosition   lightData.position.xyz
#define LightRange      lightData.position.w
#define LightDirection  lightData.direction.xyz
#define LightType       lightData.direction.w
#define LightColor      lightData.color.xyz
inline float pow5(float v)
{
    return v * v * v * v * v;
}
float MicrofacetNDF_GGX(in float3 vNormal, in float3 vHalfWay, in float fRoughness)
{
    float fRoughnessSqr = fRoughness * fRoughness;

    float fCosAlphaSqr = saturate(dot(vNormal, vHalfWay)); // Alpha is angle between normal and half-way vector
	//fCosAlphaSqr *= fCosAlphaSqr;		// need to check if can be optimized to use less instructions
    float fDiv = ((fRoughnessSqr - 1) * fCosAlphaSqr + 1);
    float fNDCoeff = fRoughnessSqr;
    fNDCoeff /= PI * (fDiv * fDiv + 1e-7f);

    half d = (fCosAlphaSqr * fRoughnessSqr - fCosAlphaSqr) * fCosAlphaSqr + 1.0f; // 2 mad
    return fRoughnessSqr / (d * d + 1e-7f);
	//return fNDCoeff;
	//float 
}
float GGX_PartialGeometry(float cosThetaN, float alpha)
{
    float cosTheta_sqr = saturate(cosThetaN * cosThetaN);
    float tan2 = (1 - cosTheta_sqr) / cosTheta_sqr;
    float GP = 2 / (1 + sqrt(1 + alpha * alpha * tan2));
    return GP;
}
float MicrofacetFresnel(in float3 LightDir, in float3 Normal, in float fRoughness)
{
    float IOR = 1.5f;
    float f0 = (1 - IOR) / (1 + IOR);
    f0 *= f0;
    // Cosine between light and normal
    float CosPhi = max(dot(LightDir, Normal), 0);

    return f0 + (1 - f0) * pow5(1 - CosPhi);
}
float GGX_Distribution(float cosThetaNH, float alpha)
{
    float alpha2 = alpha * alpha;
    float NH_sqr = saturate(cosThetaNH * cosThetaNH);
    float den = NH_sqr * alpha2 + (1.0 - NH_sqr);
    return alpha2 / (PI * den * den);
}
inline float SmithJointGGXVisibilityTerm(float NdotL, float NdotV, float roughness)
{
	// Original formulation:
	//	lambda_v	= (-1 + sqrt(a2 * (1 - NdotL2) / NdotL2 + 1)) * 0.5f;
	//	lambda_l	= (-1 + sqrt(a2 * (1 - NdotV2) / NdotV2 + 1)) * 0.5f;
	//	G			= 1 / (1 + lambda_v + lambda_l);

	// Reorder code to be more optimal
    float a = roughness;
    float a2 = a * a;

    float lambdaV = NdotL * sqrt((-NdotV * a2 + NdotV) * NdotV + a2);
    float lambdaL = NdotV * sqrt((-NdotL * a2 + NdotL) * NdotL + a2);

	// Simplify visibility term: (2.0f * NdotL * NdotV) /  ((4.0f * NdotL * NdotV) * (lambda_v + lambda_l + 1e-5f));
    return 0.5f / (lambdaV + lambdaL + 1e-5f);
}
void CalculateSpecularTerm(in float3 vNormal, in float3 vLightDir, in float3 vViewDir, in float fRoughness, out float fSpecularTerm)
{
    float3 vHalfWay = normalize(vLightDir + vViewDir);
    float fresnelTerm = MicrofacetFresnel(vLightDir, vHalfWay, fRoughness);
    float roug_sqr = fRoughness * fRoughness;
    float G = GGX_PartialGeometry(dot(vNormal, vViewDir), roug_sqr) * GGX_PartialGeometry(dot(vNormal, vLightDir), roug_sqr);
    float D = GGX_Distribution(dot(vNormal, vHalfWay), roug_sqr);
    float ndfTerm = MicrofacetNDF_GGX(vNormal, vHalfWay, fRoughness * fRoughness);
    float ndl = saturate(dot(vNormal, vLightDir));
    float ndv = saturate(dot(vNormal, vViewDir));
    float visibilityTerm = SmithJointGGXVisibilityTerm(ndl, ndv, fRoughness * fRoughness);
    /*if (ndv <= 0.0 || ndl <= 0.0)
    {
        fSpecularTerm = 0.0f;
        return;
    }*/
	//MicrofacetGeometricShadow(vNormal, vViewDir, fRoughness);
#define USE_PBR 0 
#if USE_PBR==1
    fSpecularTerm = ndfTerm * 0.25f;
#else
    fSpecularTerm = PhongSpecular(vNormal, vLightDir, -vViewDir, fRoughness) * 4.0f;
#endif
    //
    //ndfTerm * fresnelTerm;
}
void CalculateDiffuseTerm_ViewDependent(in float3 vNormal, in float3 vLightDir, in float3 vViewDir, out float fDiffuseTerm, in float fRoughness)
{
    float fLambert = LambertDiffuse(vNormal, vLightDir);
    float fFL = pow5(1 - saturate(dot(vNormal, vLightDir)));
    float fFV = pow5(1 - saturate(dot(vNormal, vViewDir)));
    float3 vHalfWay = normalize(vLightDir + vViewDir);
	
    float LdotH = saturate(dot(vHalfWay, vLightDir));

    float fd90 = 0.5 + 2 * LdotH * LdotH * fRoughness;
	// Two schlick fresnel term
    float lightScatter = (1 + (fd90 - 1) * fFL);
    float viewScatter = (1 + (fd90 - 1) * fFV);
    fDiffuseTerm = (lightScatter * viewScatter) * fLambert;
}
float CalculateAttenuation(float Range, float dis, float d)
{
    return 1.0f - smoothstep(Range * dis, Range, d);
}

float CalculateSpotCone(float3 lightDir, float3 lightPos, float spotAngle)
{
    float minCos = cos(radians(spotAngle));

    float maxCos = lerp(minCos, 1, 0.9f);
    float cosAngle = dot(lightDir, -lightPos);

    return smoothstep(minCos, maxCos, cosAngle);
}

float ComputeAttenuation(float3 lDir, float len)
{
    return 1 - saturate(dot(lDir, lDir) * len);
}

float4 main(float2 texCoord : TEXCOORD0) : COLOR
{
    float4 Parameters = TEXTURE2D_MATERIALPROPS(texCoord);
    float SpecIntensity = Parameters.x;
    float Roughness = 1 - Parameters.y;
    

    float depth;
    float3 normal;
    DecodeDepthNormal(texCoord, FarClip, depth, normal);
   // normal = mul(normal, (float3x3) InverseViewMatrix);
    
    float3 worldPosition;
    WorldPositionFromDepth(texCoord, depth, ProjectionMatrix, InverseViewMatrix, worldPosition);
    float3 ViewDir = normalize(worldPosition.xyz - InverseViewMatrix[3].xyz);

    float4 color = 0;  
   
    float3 lightPos = -normalize(worldPosition.xyz - LightPosition.xyz);
    float dirLen = length(worldPosition - LightPosition);
    
    float s = LightType ? 0.95f : 0.15f;
    float atten = 1.0f - smoothstep(LightRange * s, LightRange, dirLen);
    atten = 1.0f - pow(saturate(dirLen / LightRange), 30);
    if (LightType)
    {
        float angle = 40.0;
        float minCos = cos(radians(angle));

        float maxCos = lerp(minCos, 1, 0.9f);
        float cosAngle = dot(LightDirection, -lightPos);

        atten *= smoothstep(minCos, maxCos, cosAngle);
    }
       
    float3 FinalDiffuseTerm = float3(0, 0, 0);
    float FinalSpecularTerm = 0;
    float DiffuseTerm, SpecularTerm;
    CalculateDiffuseTerm_ViewDependent(normal, lightPos.xyz, -ViewDir, DiffuseTerm, Roughness);
    CalculateSpecularTerm(normal, lightPos.xyz, -ViewDir, Roughness, SpecularTerm);
    FinalDiffuseTerm += DiffuseTerm * atten * LightColor;
    FinalSpecularTerm += SpecularTerm * atten * SpecIntensity;
    float4 Lighting = float4(FinalDiffuseTerm, FinalSpecularTerm);
    color.xyzw = Lighting;
    return color;
}