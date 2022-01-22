//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

Texture2D gScribbleTex : register(t0) {};
SamplerState gSampler : register(s0) {};

cbuffer ObjectConstantBuffer : register(b0)
{
    float4x4 world;
    float4x4 worldCenterSquare;
    float4x4 worldViewProjection;
    float3 eyePos;
    float2 texOffset;
    float3 lightDirection;
    float4 gFogColor;
    float gFogStart;
    float gFogRange;
};

struct PSInput
{
    float3 posW : POSITION;
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 tex : TEXCOORD;
};

struct Material
{
    float4 diffuseAlbedo;
    float3 fresnelR0;
    float shininess;
};

float4 ComputeDirectLight(float lightStrength, float3 lightDirection, float3 normal, float3 toEye, Material mat);
float3 BlinnPhong(float lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat);
float3 SchlickFresnel(float3 r0, float3 normal, float3 lightVec);

PSInput VSMain(float4 position : POSITION, float3 normal : NORMAL, float2 tex : TEXCOORD)
{
    PSInput result;


#ifdef NDC // use normalized device coordinates
    result.position = mul(float4(position.xyz, 1.0f), worldCenterSquare);
    result.normal = normal;
#else
    float4 posW = mul(float4(position.xyz, 1.0f), world);
    result.posW = posW.xyz;

    result.position = mul(position, worldViewProjection);
    result.normal = mul(normal, (float3x3)worldViewProjection);
#endif

    result.tex = tex;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    const float4 ambientLight = float4(0.0f, 0.0f, 0.0f, 1.0f);
    //const float4 diffuseAlbedo = float4(1.0f, 1.0f, 1.0f, 1.0f);
    //const float4 diffuseAlbedo = gScribbleTex.Sample(gSampler, input.tex + texOffset);
    const float4 diffuseAlbedo = gScribbleTex.Sample(gSampler, input.tex) * float4(1.0f, 1.0f, 1.0f, 0.5f);

#ifdef ALPHA_TEST
    clip(diffuseAlbedo.a - 0.1f);
#endif

    const float3 fresnelR0 = float3(0.04f, 0.04f, 0.04f);
    const float shininess = 0.5f;

    const float lightStrength = 0.3f;
    //const float3 lightDirection = float3(1.5f, -1.0f, 1.5f);

    Material mat = { diffuseAlbedo, fresnelR0, shininess };

    float3 toEyeW = eyePos - input.posW;
    float distToEye = length(toEyeW);
    toEyeW /= distToEye; // normalize
    //toEyeW = normalize(toEyeW); // normalize

    // Ambient 계산
    float4 ambient = ambientLight * diffuseAlbedo;

    // 빛이 들어오는 방향
    float3 toEye = normalize(eyePos - input.position);

    // 빛을 계산한다.
    //float4 directLight = float4(1.0f, 1.0f, 1.0f, 1.0f);
    float4 directLight = ComputeDirectLight(lightStrength, lightDirection, input.normal, toEye, mat);

    float4 resultColor = ambient + directLight;
#ifdef FOG
    float fogAmount = saturate((distToEye - gFogStart) / gFogRange);
    resultColor = lerp(resultColor, gFogColor, fogAmount);
#endif
    resultColor.a = diffuseAlbedo.a;

    return resultColor;
}

float4 ComputeDirectLight(float lightStrength, float3 lightDirection, float3 normal, float3 toEye, Material mat)
{
    float3 lightVec = -lightDirection;

    // 람베르트 코사인 법칙

    float lambert = max(dot(lightVec, normal), 0.0f);
    float3 color = BlinnPhong(lambert * lightStrength, lightVec, normal, toEye, mat);

    return float4(color, 0.0f);
}

float3 BlinnPhong(float lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat)
{
    const float m = mat.shininess * 256.0f;
    float3 halfVec = normalize(toEye + lightVec);

    float cosTheta = saturate(dot(halfVec, normal));
    float roughnessFactor = (m + 8.0f) * pow(cosTheta, m) / 8.0f;
    float3 fresnelFactor = SchlickFresnel(mat.fresnelR0, halfVec, lightVec);

    float3 specAlbedo = fresnelFactor * roughnessFactor;
    specAlbedo = specAlbedo / (specAlbedo + 1.0f);

    return (mat.diffuseAlbedo.rgb + specAlbedo) * lightStrength;
}

float3 SchlickFresnel(float3 r0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));
    float f0 = 1.0f - cosIncidentAngle;
    float reflectPercent = r0 + (1.0f - r0) * (f0 * f0 * f0 * f0 * f0);

    return reflectPercent;
}