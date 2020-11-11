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

StructuredBuffer<VertexOut> VertPayloads;
StructuredBuffer<uint>	PrimIdxPayloads;

uint3 UnpackPrimitive(uint primitive)
{
	// Unpacks a 10 bits per index triangle from a 32-bit uint.
	return uint3(primitive & 0x3FF, (primitive >> 10) & 0x3FF, (primitive >> 20) & 0x3FF);
}

VertexOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	const uint primitive = PrimIdxPayloads[126 * iid + vid / 3];
	const uint index = UnpackPrimitive(primitive)[vid % 3];
	const VertexOut vert = VertPayloads[64 * iid + index];

	return vert;
}
