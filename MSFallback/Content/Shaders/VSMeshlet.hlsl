//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedConst.h"

struct VertexOut
{
	float4 PositionHS   : SV_POSITION;
	float3 PositionVS   : POSITION;
	float3 Normal       : NORMAL;
	uint   MeshletIndex : COLOR;
};

cbuffer PerDispatch : FALLBACK_LAYER_PAYLOAD_REG(b0)
{
	uint BatchIdx;
}

StructuredBuffer<VertexOut> VertexPayloads;

VertexOut main(uint vid : SV_VertexID)
{
	return VertexPayloads[BATCH_VERTEX_SIZE * BatchIdx + vid];
}
