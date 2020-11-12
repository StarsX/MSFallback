//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

struct VertexOut
{
	float4 PositionHS   : SV_POSITION;
	float3 PositionVS   : POSITION;
	float3 Normal       : NORMAL;
	uint   MeshletIndex : COLOR;
};

StructuredBuffer<VertexOut> VertexPayloads;

VertexOut main(uint vid : SV_VertexID)
{
	return VertexPayloads[vid];
}
