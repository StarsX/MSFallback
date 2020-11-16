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
	float3 PositionVS   : POSITION0;
	float3 Normal       : NORMAL0;
	uint   MeshletIndex : COLOR0;
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
ByteAddressBuffer			UniqueVertexIndices : register(t2);
StructuredBuffer<uint>		PrimitiveIndices;
StructuredBuffer<CullData>	MeshletCullData;


// Rotates a vector, v0, about an axis by some angle
float3 RotateVector(float3 v0, float3 axis, float angle)
{
	float cs = cos(angle);
	return cs * v0 + sin(angle) * cross(axis, v0) + (1 - cs) * dot(axis, v0) * axis;
}
