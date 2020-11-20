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
		const uint indexCount = 3 * MAX_PRIM_COUNT * AS_GROUP_SIZE; \
		DispatchMeshArgs[base] = indexCount; \
		DispatchMeshArgs[base + 1] = 1; \
		DispatchMeshArgs[base + 2] = indexCount * gid; \
		DispatchMeshArgs[base + 5] = x; \
		DispatchMeshArgs[base + 6] = y; \
		DispatchMeshArgs[base + 7] = z; \
	} \
	if (gtid < visibleCount) \
		DispatchMeshArgs[base + gtid + 8] = s_Payload.MeshletIndices[gtid]; \
}

#include "ASMeshlet.hlsl"
