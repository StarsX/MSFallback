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

	if (gtid < m.PrimCount)
		PrimIdxPayloads[126 * gid + gtid] = tri.x | (tri.y << 10) | (tri.z << 20);

	if (gtid < m.VertCount) VertPayloads[64 * gid + gtid] = vert;
}
