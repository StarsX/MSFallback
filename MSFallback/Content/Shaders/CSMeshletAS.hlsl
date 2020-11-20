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
		DispatchMeshArgs[base] = x; \
		DispatchMeshArgs[base + 1] = y; \
		DispatchMeshArgs[base + 2] = z; \
	} \
	if (gtid < visibleCount) \
		DispatchMeshArgs[base + gtid + 3] = s_Payload.MeshletIndices[gtid]; \
}

#include "ASMeshlet.hlsl"
