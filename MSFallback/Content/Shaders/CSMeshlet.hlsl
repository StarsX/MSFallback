//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "MSMeshlet.hlsl"

RWStructuredBuffer<VertexOut> VertexPayloads;
RWBuffer<uint> IndexPayloads;

[numthreads(128, 1, 1)]
void main(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID)
{
	uint3 tri;
	VertexOut vert;
	const Meshlet m = MeshShader(gtid, gid, tri, vert);

	if (gtid < MAX_PRIM_COUNT)
	{
		const uint baseAddr = 3 * (MAX_PRIM_COUNT * gid + gtid);
		const uint baseIdx = MAX_VERT_COUNT * gid;

		[unroll]
		for (uint i = 0; i < 3; ++i)
			IndexPayloads[baseAddr + i] = gtid < m.PrimCount ? baseIdx + tri[i] : 0xffffffff;
	}

	if (gtid < m.VertCount) VertexPayloads[MAX_VERT_COUNT * gid + gtid] = vert;
}
