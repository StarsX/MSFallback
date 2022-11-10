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

#define GET_MESHLET_IDX(i) DispatchMeshArgs[sizeof(DispatchArgs) / sizeof(uint) * BatchIdx + i]
#define VERT_IDX(i) (vid = i)
#define PRIM_IDX(i) (pid = i)
#define indices uint pid, out
#define vertices MeshOutCounts moc, out uint vid, out

cbuffer PerDispatch : FALLBACK_LAYER_PAYLOAD_REG(b0)
{
	uint BatchIdx;
}

#define main MSMain
#include "MSMeshlet.hlsl"
#undef main

RWStructuredBuffer<VertexOut> VertexPayloads : FALLBACK_LAYER_PAYLOAD_REG(u0);
RWBuffer<uint> IndexPayloads : FALLBACK_LAYER_PAYLOAD_REG(u1);

[numthreads(MS_GROUP_SIZE, 1, 1)]
void main(uint dtid : SV_DispatchThreadID, uint gtid : SV_GroupThreadID, uint gid : SV_GroupID)
{
	VertexOut verts[MAX_VERTS];
	uint3 tris[MAX_PRIMS];
	Payload payload = (Payload)0;
	MeshOutCounts moc = (MeshOutCounts)0;
	uint pid, vid;
	MSMain(dtid, gtid, gid, payload, moc, vid, verts, pid, tris);

	const uint meshletIdx = BATCH_MESHLET_SIZE * BatchIdx + gid;

	if (pid < MAX_PRIMS)
	{
		const uint baseAddr = 3 * (MAX_PRIMS * meshletIdx + pid);
		const uint baseIdx = MAX_VERTS * gid;

		[unroll]
		for (uint i = 0; i < 3; ++i)
			IndexPayloads[baseAddr + i] = pid < moc.PrimCount ? baseIdx + tris[pid][i] : 0xffff;
	}

	if (vid < moc.VertCount) VertexPayloads[MAX_VERTS * meshletIdx + vid] = verts[vid];
}
