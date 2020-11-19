//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

struct MeshOutCounts
{
	uint VertCount;
	uint PrimCount;
};

#define SetMeshOutputCounts(vertCount, primCount) \
{ \
	moc.VertCount = vertCount; \
	moc.PrimCount = primCount; \
}

#define OUT_IDX(i) 0
#define vertices MeshOutCounts moc, out
#define indices

#define main MSMain
#include "MSMeshlet.hlsl"
#undef main

RWStructuredBuffer<VertexOut> VertexPayloads;
RWBuffer<uint> IndexPayloads;

[numthreads(MS_GROUP_SIZE, 1, 1)]
void main(uint dtid : SV_DispatchThreadID, uint gtid : SV_GroupThreadID, uint gid : SV_GroupID)
{
	VertexOut verts[MAX_VERT_COUNT];
	uint3 tris[MAX_PRIM_COUNT];
	MeshOutCounts moc = (MeshOutCounts)0;
	MSMain(dtid, gtid, gid, moc, verts, tris);

	if (gtid < MAX_PRIM_COUNT)
	{
		const uint baseAddr = 3 * (MAX_PRIM_COUNT * gid + gtid);
		const uint baseIdx = MAX_VERT_COUNT * gid;

		[unroll]
		for (uint i = 0; i < 3; ++i)
			IndexPayloads[baseAddr + i] = gtid < moc.PrimCount ? baseIdx + tris[0][i] : 0xffffffff;
	}

	if (gtid < moc.VertCount) VertexPayloads[MAX_VERT_COUNT * gid + gtid] = verts[0];
}
