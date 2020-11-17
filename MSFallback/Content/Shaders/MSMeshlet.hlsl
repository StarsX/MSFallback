//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "MeshletCommon.hlsli"

#define MAX_PRIM_COUNT	126
#define MAX_VERT_COUNT	64

// Packs/unpacks a 10-bit index triangle primitive into/from a uint.
uint3 UnpackPrimitive(uint primitive)
{
	return uint3(primitive & 0x3FF, (primitive >> 10) & 0x3FF, (primitive >> 20) & 0x3FF);
}

//--------------------------------
// Data Loaders

uint GetVertexIndex(Meshlet m, uint localIndex)
{
	localIndex = m.VertOffset + localIndex;

	if (MeshInfo.IndexSize == 4)
	{
		return UniqueVertexIndices.Load(localIndex * 4);
	}
	else // Global vertex index width is 16-bit
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

uint3 GetPrimitive(Meshlet m, uint index)
{
	return UnpackPrimitive(PrimitiveIndices[m.PrimOffset + index]);
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

Meshlet MeshShader(uint gtid, uint meshletIndex, out uint3 tri, out VertexOut vert)
{
	// Load the meshlet
	const Meshlet m = Meshlets[meshletIndex];

	//--------------------------------------------------------------------
	// Export Primitive & Vertex Data

	if (gtid < m.VertCount)
	{
		const uint vertexIndex = GetVertexIndex(m, gtid);
		vert = GetVertexAttributes(meshletIndex, vertexIndex);
	}

	if (gtid < m.PrimCount) tri = GetPrimitive(m, gtid);

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
	// Load the meshlet
	uint3 tri;
	VertexOut vert = (VertexOut)0;
	const Meshlet m = MeshShader(gtid, gid, tri, vert);

	// Our vertex and primitive counts come directly from the meshlet
	SetMeshOutputCounts(m.VertCount, m.PrimCount);

	//--------------------------------------------------------------------
	// Export Primitive & Vertex Data

	if (gtid < m.VertCount) verts[gtid] = vert;
	if (gtid < m.PrimCount) tris[gtid] = tri;
}
#endif

#if _NATIVE_AS
[NumThreads(128, 1, 1)]
[OutputTopology("triangle")]
void main(
	uint dtid : SV_DispatchThreadID,
	uint gtid : SV_GroupThreadID,
	uint gid : SV_GroupID,
	in payload Payload payload,
	out vertices VertexOut verts[MAX_VERT_COUNT],
	out indices uint3 tris[MAX_PRIM_COUNT]
)
{
	// Load the meshlet from the AS payload data
	const uint meshletIndex = payload.MeshletIndices[gid];

	// Catch any out-of-range indices (in case too many MS threadgroups were dispatched from AS)
	if (meshletIndex >= MeshInfo.MeshletCount) return;

	// Load the meshlet
	uint3 tri;
	VertexOut vert = (VertexOut)0;
	const Meshlet m = MeshShader(gtid, meshletIndex, tri, vert);

	// Our vertex and primitive counts come directly from the meshlet
	SetMeshOutputCounts(m.VertCount, m.PrimCount);

	//--------------------------------------------------------------------
	// Export Primitive & Vertex Data

	if (gtid < m.VertCount) verts[gtid] = vert;
	if (gtid < m.PrimCount) tris[gtid] = tri;
}
#endif
