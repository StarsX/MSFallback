//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedConst.h"

RWStructuredBuffer<uint> DispatchMeshArgs : FALLBACK_LAYER_PAYLOAD_REG(u0);

#define DispatchMesh(x, y, z, payload) \
{ \
	const uint stride = sizeof(DispatchArgs) / sizeof(uint); \
	const uint base = stride * gid; \
	if (gtid == 0) \
	{ \
		const uint indexCount = 3 * MAX_PRIMS * AS_GROUP_SIZE; \
		DispatchMeshArgs[base] = gid; \
		DispatchMeshArgs[base + 1] = indexCount; \
		DispatchMeshArgs[base + 2] = 1; \
		DispatchMeshArgs[base + 3] = indexCount * gid; \
		DispatchMeshArgs[base + 6] = gid; \
		DispatchMeshArgs[base + 7] = x; \
		DispatchMeshArgs[base + 8] = y; \
		DispatchMeshArgs[base + 9] = z; \
	} \
	if (gtid < visibleCount) \
		DispatchMeshArgs[base + gtid + 10] = s_Payload.MeshletIndices[gtid]; \
}

#include "ASMeshlet.hlsl"
