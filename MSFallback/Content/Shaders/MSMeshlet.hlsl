//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "MeshletCommon.hlsli"

#define MAX_PRIM_COUNT	126
#define MAX_VERT_COUNT	64

/////
// Data Loaders
uint3 UnpackPrimitive(uint primitive)
{
	// Unpacks a 10 bits per index triangle from a 32-bit uint.
	return uint3(primitive & 0x3FF, (primitive >> 10) & 0x3FF, (primitive >> 20) & 0x3FF);
}

uint3 GetPrimitive(Meshlet m, uint index)
{
	return UnpackPrimitive(PrimitiveIndices[m.PrimOffset + index]);
}

uint GetVertexIndex(Meshlet m, uint localIndex)
{
	localIndex = m.VertOffset + localIndex;

	if (MeshInfo.IndexSize == 4) // 32-bit Vertex Indices
	{
		return UniqueVertexIndices.Load(localIndex * 4);
	}
	else // 16-bit Vertex Indices
	{
		// Byte address must be 4-byte aligned.
		const uint wordOffset = (localIndex & 0x1);
		const uint byteOffset = (localIndex / 2) * 4;

		// Grab the pair of 16-bit indices, shift & mask off proper 16-bits.
		const uint indexPair = UniqueVertexIndices.Load(byteOffset);
		const uint index = (indexPair >> (wordOffset * 16)) & 0xffff;

		return index;
	}
}

VertexOut GetVertexAttributes(uint meshletIndex, uint vertexIndex)
{
	Vertex v = Vertices[vertexIndex];

	const float4 positionWS = { mul(float4(v.Position, 1.0), Instance.World), 1.0 };

	VertexOut vout;
	vout.PositionVS = mul(positionWS, Constants.View);
	vout.PositionHS = mul(positionWS, Constants.ViewProj);
	vout.Normal = mul(v.Normal, Instance.WorldIT);
	vout.MeshletIndex = meshletIndex;

	return vout;
}

Meshlet MeshShader(uint gtid, uint gid, out uint3 tri, out VertexOut vert)
{
	const Meshlet m = Meshlets[gid];

	if (gtid < m.PrimCount) tri = GetPrimitive(m, gtid);

	if (gtid < m.VertCount)
	{
		const uint vertexIndex = GetVertexIndex(m, gtid);
		vert = GetVertexAttributes(gid, vertexIndex);
	}

	return m;
}

#if _NATIVE_MS
[NumThreads(128, 1, 1)]
[OutputTopology("triangle")]
void main(
	uint gtid : SV_GroupThreadID,
	uint gid : SV_GroupID,
	out indices uint3 tris[MAX_PRIM_COUNT],
	out vertices VertexOut verts[MAX_VERT_COUNT]
)
{
	uint3 tri;
	VertexOut vert = (VertexOut)0;
	const Meshlet m = MeshShader(gtid, gid, tri, vert);

	SetMeshOutputCounts(m.VertCount, m.PrimCount);

	if (gtid < m.PrimCount) tris[gtid] = tri;
	if (gtid < m.VertCount) verts[gtid] = vert;
}
#endif
