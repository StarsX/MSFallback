//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

struct Vertex
{
	float3 Position;
	float3 Normal;
};

struct VertexOut
{
	float4 PositionHS   : SV_Position;
	float3 PositionVS   : POSITION0;
	float3 Normal       : NORMAL0;
	uint   MeshletIndex : COLOR0;
};

struct Meshlet
{
	uint VertCount;
	uint VertOffset;
	uint PrimCount;
	uint PrimOffset;
};

cbuffer Constants
{
	float4x4 Globals_World;
	float4x4 Globals_WorldView;
	float4x4 Globals_WorldViewProj;
	uint     Globals_DrawMeshlets;
};

cbuffer MeshInfo
{
	uint MeshInfo_IndexBytes;
	uint MeshInfo_MeshletOffset;
}

StructuredBuffer<Vertex>  Vertices;
StructuredBuffer<Meshlet> Meshlets;
ByteAddressBuffer         UniqueVertexIndices;
StructuredBuffer<uint>    PrimitiveIndices;

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

	if (MeshInfo_IndexBytes == 4) // 32-bit Vertex Indices
	{
		return UniqueVertexIndices.Load(localIndex * 4);
	}
	else // 16-bit Vertex Indices
	{
		// Byte address must be 4-byte aligned.
		uint wordOffset = (localIndex & 0x1);
		uint byteOffset = (localIndex / 2) * 4;

		// Grab the pair of 16-bit indices, shift & mask off proper 16-bits.
		uint indexPair = UniqueVertexIndices.Load(byteOffset);
		uint index = (indexPair >> (wordOffset * 16)) & 0xffff;

		return index;
	}
}

VertexOut GetVertexAttributes(uint meshletIndex, uint vertexIndex)
{
	Vertex v = Vertices[vertexIndex];

	VertexOut vout;
	vout.PositionVS = mul(float4(v.Position, 1), Globals_WorldView).xyz;
	vout.PositionHS = mul(float4(v.Position, 1), Globals_WorldViewProj);
	vout.Normal = mul(float4(v.Normal, 0), Globals_World).xyz;
	vout.MeshletIndex = meshletIndex;

	return vout;
}

Meshlet MeshShader(uint gtid, uint gid, out uint3 tri, out VertexOut vert)
{
	const Meshlet m = Meshlets[MeshInfo_MeshletOffset + gid];

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
	out indices uint3 tris[126],
	out vertices VertexOut verts[64]
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
