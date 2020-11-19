//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedConst.h"

RWByteAddressBuffer DispatchMeshArgs;

#define DispatchMesh(x, y, z, payload) \
{ \
	const uint stride = sizeof(DispatchArgs); \
	const uint _xyz = stride * gid; \
	if (gtid == 0) DispatchMeshArgs.Store3(_xyz, uint3(x, y, z)); \
	if (gtid < visibleCount) DispatchMeshArgs.Store(_xyz + sizeof(uint3), s_Payload.MeshletIndices[gtid]); \
}

#define main ASMain
#include "ASMeshlet.hlsl"
#undef main

[numthreads(AS_GROUP_SIZE, 1, 1)]
void main(uint dtid : SV_DispatchThreadID, uint gtid : SV_GroupThreadID, uint gid : SV_GroupID)
{
	ASMain(gtid, dtid, gid);
}
