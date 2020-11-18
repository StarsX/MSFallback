//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedConst.h"
#include "MeshletUtils.hlsli"

struct Vertex
{
	float3 Position;
	float3 Normal;
};

struct VertexOut
{
	float4 PositionHS   : SV_Position;
	float3 PositionVS   : POSITION;
	float3 Normal       : NORMAL;
	uint   MeshletIndex : COLOR;
};

struct Payload
{
	uint MeshletIndices[AS_GROUP_SIZE];
};

ConstantBuffer<Constants>	Constants;
ConstantBuffer<MeshInfo>	MeshInfo;
ConstantBuffer<Instance>	Instance;
StructuredBuffer<Vertex>	Vertices;
StructuredBuffer<Meshlet>	Meshlets;
ByteAddressBuffer			UniqueVertexIndices;
StructuredBuffer<uint>		PrimitiveIndices;
StructuredBuffer<CullData>	MeshletCullData : register(t4);

// Rotates a vector, v0, about an axis by some angle
float3 RotateVector(float3 v0, float3 axis, float angle)
{
	const float cs = cos(angle);

	return cs * v0 + sin(angle) * cross(axis, v0) + (1.0 - cs) * dot(axis, v0) * axis;
}
