//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "MeshletCommon.hlsli"

#define MS_GROUP_SIZE 128

#ifndef OUT_IDX
#define OUT_IDX(i) i
#endif

#ifndef GET_MESHLET_IDX
#define GET_MESHLET_IDX(i) payload.MeshletIndices[i]
#endif

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

	const float4 positionWS = mul(float4(v.Position, 1.0), Instance.World);

	VertexOut vout;
	vout.PositionVS = mul(positionWS, Constants.View);
	vout.PositionHS = mul(positionWS, Constants.ViewProj);
	vout.Normal = mul(v.Normal, (float3x3)Instance.WorldIT);
	vout.MeshletIndex = meshletIndex;

	return vout;
}

[NumThreads(MS_GROUP_SIZE, 1, 1)]
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
	const uint meshletIndex = GET_MESHLET_IDX(gid);

	// Catch any out-of-range indices (in case too many MS threadgroups were dispatched from AS)
	if (meshletIndex >= MeshInfo.MeshletCount) return;

	// Load the meshlet
	Meshlet m = Meshlets[meshletIndex];

	// Our vertex and primitive counts come directly from the meshlet
	SetMeshOutputCounts(m.VertCount, m.PrimCount);

	//--------------------------------------------------------------------
	// Export Primitive & Vertex Data

	if (gtid < m.VertCount)
	{
		const uint vertexIndex = GetVertexIndex(m, gtid);
		verts[OUT_IDX(gtid)] = GetVertexAttributes(meshletIndex, vertexIndex);
	}

	if (gtid < m.PrimCount) tris[OUT_IDX(gtid)] = GetPrimitive(m, gtid);
}
