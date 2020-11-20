//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedConst.h"

struct MeshOutCounts
{
	uint VertCount;
	uint PrimCount;
};

StructuredBuffer<uint> DispatchMeshArgs : FALLBACK_LAYER_PAYLOAD_REG(t0);

#define SetMeshOutputCounts(vertCount, primCount) \
{ \
	moc.VertCount = vertCount; \
	moc.PrimCount = primCount; \
}

#define GET_MESHLET_IDX(i) DispatchMeshArgs[i]
#define OUT_IDX(i) 0
#define indices
#define vertices MeshOutCounts moc, out

#define main MSMain
#include "MSMeshlet.hlsl"
#undef main

cbuffer PerDispatch : FALLBACK_LAYER_PAYLOAD_REG(b0)
{
	uint BatchIdx;
}

RWStructuredBuffer<VertexOut> VertexPayloads : FALLBACK_LAYER_PAYLOAD_REG(u0);
RWBuffer<uint> IndexPayloads : FALLBACK_LAYER_PAYLOAD_REG(u1);

[numthreads(MS_GROUP_SIZE, 1, 1)]
void main(uint dtid : SV_DispatchThreadID, uint gtid : SV_GroupThreadID, uint gid : SV_GroupID)
{
	VertexOut verts[MAX_VERT_COUNT];
	uint3 tris[MAX_PRIM_COUNT];
	Payload payload = (Payload)0;
	MeshOutCounts moc = (MeshOutCounts)0;
	MSMain(dtid, gtid, gid, payload, moc, verts, tris);

	const uint meshletIdx = BATCH_MESHLET_SIZE * BatchIdx + gid;

	if (gtid < MAX_PRIM_COUNT)
	{
		const uint baseAddr = 3 * (MAX_PRIM_COUNT * meshletIdx + gtid);
		const uint baseIdx = MAX_VERT_COUNT * gid;

		[unroll]
		for (uint i = 0; i < 3; ++i)
			IndexPayloads[baseAddr + i] = gtid < moc.PrimCount ? baseIdx + tris[0][i] : 0xffff;
	}

	if (gtid < moc.VertCount) VertexPayloads[MAX_VERT_COUNT * meshletIdx + gtid] = verts[0];
}
