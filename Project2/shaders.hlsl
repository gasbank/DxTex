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
// C++ 코드에서 gConstantBufferData에 들어있는 내용이 채워진다.
cbuffer ObjectConstantBuffer : register(b0)
{
    float4x4 worldViewProjection;
    float3 eyePos;
};

struct PSInput
{
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

SamplerState gsamAnisotropicWrap : register(s0);
Texture2D gScribbleMap : register(t0);

PSInput VSMain(float4 position : POSITION, float3 normal : NORMAL, float2 tex : TEXCOORD)
{
    PSInput result;

    result.position = mul(position, worldViewProjection);
    result.normal = mul(normal, (float3x3)worldViewProjection);
    result.tex = tex;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    const float4 ambientLight = float4(0.0f, 0.0f, 0.0f, 1.0f);
    const float4 diffuseAlbedo = float4(1.0f, 1.0f, 1.0f, 1.0f);
    const float3 fresnelR0 = float3(0.04f, 0.04f, 0.04f);
    const float shininess = 0.5f;

    const float lightStrength = 0.3f;
    const float3 lightDirection = float3(1.5f, -1.0f, 1.5f);

    Material mat = { diffuseAlbedo, fresnelR0, shininess };

    // Ambient 계산
    float4 ambient = ambientLight * diffuseAlbedo;

    // 빛이 들어오는 방향
    float3 toEye = normalize(eyePos - input.position);

    // TODO: 빛을 계산한다.
    float4 directLight = float4(1, 1, 1, 1);

    float4 resultColor = ambient + directLight;
    resultColor.a = diffuseAlbedo.a;

    return resultColor;
}