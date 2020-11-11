//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

struct VertexOut
{
	float4 PositionHS   : SV_Position;
	float3 PositionVS   : POSITION0;
	float3 Normal       : NORMAL0;
	uint   MeshletIndex : COLOR0;
};

cbuffer Constants
{
	float4x4 Globals_World;
	float4x4 Globals_WorldView;
	float4x4 Globals_WorldViewProj;
	uint     Globals_DrawMeshlets;
};

float4 main(VertexOut input) : SV_TARGET
{
	const float ambientIntensity = 0.1;
	const float3 lightColor = float3(1, 1, 1);
	const float3 lightDir = -normalize(float3(1, -1, 1));

	float3 diffuseColor;
	float shininess;
	if (Globals_DrawMeshlets)
	{
		const uint meshletIndex = input.MeshletIndex;
		diffuseColor = float3(
			float(meshletIndex & 1),
			float(meshletIndex & 3) / 4,
			float(meshletIndex & 7) / 8);
		shininess = 16.0;
	}
	else
	{
		diffuseColor = 0.8;
		shininess = 64.0;
	}

	const float3 normal = normalize(input.Normal);

	// Do some fancy Blinn-Phong shading!
	const float cosAngle = saturate(dot(normal, lightDir));
	const float3 viewDir = -normalize(input.PositionVS);
	const float3 halfAngle = normalize(lightDir + viewDir);

	float blinnTerm = saturate(dot(normal, halfAngle));
	blinnTerm = cosAngle != 0.0 ? blinnTerm : 0.0;
	blinnTerm = pow(blinnTerm, shininess);

	const float3 finalColor = (cosAngle + blinnTerm + ambientIntensity) * diffuseColor;

	return float4(finalColor, 1.0);
}
