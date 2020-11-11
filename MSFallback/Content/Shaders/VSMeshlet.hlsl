//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#define MAX_PRIM_COUNT	126
#define MAX_VERT_COUNT	64

struct VertexOut
{
	float4 PositionHS   : SV_POSITION;
	float3 PositionVS   : POSITION;
	float3 Normal       : NORMAL;
	uint   MeshletIndex : COLOR;
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
	VertexOut vert = (VertexOut)0;
	const uint primitive = PrimIdxPayloads[MAX_PRIM_COUNT * iid + vid / 3];

	if (primitive < 0xffffffff)
	{
		const uint index = UnpackPrimitive(primitive)[vid % 3];
		vert = VertPayloads[MAX_VERT_COUNT * iid + index];
	}

	return vert;
}
