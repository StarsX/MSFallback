//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "MSMeshlet.hlsl"

RWStructuredBuffer<VertexOut> VertPayloads;
RWStructuredBuffer<uint>	PrimIdxPayloads;

[numthreads(128, 1, 1)]
void main(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID)
{
	uint3 tri;
	VertexOut vert;
	const Meshlet m = MeshShader(gtid, gid, tri, vert);

	if (gtid < MAX_PRIM_COUNT)
		PrimIdxPayloads[MAX_PRIM_COUNT * gid + gtid] = gtid < m.PrimCount ?
			tri.x | (tri.y << 10) | (tri.z << 20) : 0xffffffff;

	if (gtid < m.VertCount) VertPayloads[MAX_VERT_COUNT * gid + gtid] = vert;
}
