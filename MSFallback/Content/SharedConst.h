//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#define CLEAR_COLOR			0.0f, 0.2f, 0.4f
//#define CORN_FLOWER_BLUE	0.392156899, 0.584313750, 0.929411829

static const float g_zNear = 1.0f;
static const float g_zFar = 300.0f;

#define REG_SPACE(s) space##s
#define FALLBACK_LAYER_PAYLOAD_SPACE 214743648
#define FALLBACK_LAYER_PAYLOAD_REG_SPACE(r, s) register (r, REG_SPACE(s))
#define FALLBACK_LAYER_PAYLOAD_REG(r) FALLBACK_LAYER_PAYLOAD_REG_SPACE(r, FALLBACK_LAYER_PAYLOAD_SPACE)

#define MAX_PRIMS 126
#define MAX_VERTS 64

//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#define THREADS_PER_WAVE 32
#define AS_GROUP_SIZE THREADS_PER_WAVE

#define CULL_FLAG 0x1
#define MESHLET_FLAG 0x2

#ifdef __cplusplus
using float4x4 = DirectX::XMFLOAT4X4;
using float4x3 = DirectX::XMFLOAT3X4;
using float3x3 = DirectX::XMFLOAT3X4;
using float4 = DirectX::XMFLOAT4;
using float3 = DirectX::XMFLOAT3;
using float2 = DirectX::XMFLOAT2;
using uint = uint32_t;
#endif

#ifdef __cplusplus
_declspec(align(256u))
#endif
struct Instance
{
	float4x4 World;
	float4x3 WorldIT;
	float    Scale;
	uint     Flags;
};

#ifdef __cplusplus
_declspec(align(256u))
#endif
struct Constants
{
	float4x3    View;
	float4x4    ViewProj;
	float4      Planes[6];

	float3      ViewPosition;
	uint        HighlightedIndex;

	float3      CullViewPosition;
	uint        SelectedIndex;

	uint        DrawMeshlets;
};


struct DispatchArgs
{
	struct DrawIndexedArgs
	{
		uint BatchIdx;
		uint IndexCountPerInstance;
		uint InstanceCount;
		uint StartIndexLocation;
		int BaseVertexLocation;
		uint StartInstanceLocation;
	} DrawArgs;
	struct ASDispatchMeshArgs
	{
		uint BatchIdx;
		uint x;
		uint y;
		uint z;
		uint MeshletIndices[AS_GROUP_SIZE];
	} ASDispatchArgs;
};

#define BATCH_MESHLET_SIZE AS_GROUP_SIZE
#define BATCH_VERTEX_SIZE (MAX_VERTS * BATCH_MESHLET_SIZE)
