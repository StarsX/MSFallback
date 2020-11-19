//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "Ultimate/XUSGUltimate.h"
#include "Model.h"

class Renderer
{
public:
	struct ObjectDef
	{
		DirectX::XMFLOAT3 Position;
		DirectX::XMFLOAT3 Rotation;
		float    Scale;

		bool     Cull;
		bool     DrawMeshlets;
	};

	Renderer(const XUSG::Device& device);
	virtual ~Renderer();

	bool Init(XUSG::CommandList* pCommandList, uint32_t width, uint32_t height,
		XUSG::Format rtFormat, std::vector<XUSG::Resource>& uploaders,
		uint32_t objCount, const std::wstring* pFileNames, const ObjectDef* pObjDefs, bool isMSSupported);

	void UpdateFrame(uint32_t frameIndex, DirectX::CXMMATRIX view,
		const DirectX::XMMATRIX* pProj, const DirectX::XMFLOAT3& eyePt);
	void Render(XUSG::Ultimate::CommandList* pCommandList, uint32_t frameIndex,
		const XUSG::Descriptor& rtv, bool useMeshShader = true);

	static const uint32_t FrameCount = 3;

protected:
	enum PipelineLayoutIndex : uint8_t
	{
		MESHLET_NATIVE_LAYOUT,
		MESHLET_CS_FOR_AS_LAYOUT,
		MESHLET_CS_FOR_MS_LAYOUT,
		MESHLET_VS_LAYOUT,

		NUM_PIPELINE_LAYOUT
	};

	enum PipelineLayoutSlot : uint8_t
	{
		CBV_GLOBALS,
		CBV_MESHINFO,
		CBV_INSTANCE,
		SRVS,
		UAVS,
		SRV_AS_PAYLOADS
	};

	enum PipelineIndex : uint8_t
	{
		MESHLET_NATIVE,
		MESHLET_CS_FOR_AS,
		MESHLET_CS_FOR_MS,
		MESHLET_VS,

		NUM_PIPELINE
	};

	enum ComputeShaderID : uint8_t
	{
		CS_MESHLET_AS,
		CS_MESHLET_MS
	};

	enum VertexShaderID : uint8_t
	{
		VS_MESHLET
	};

	enum AmplificationShaderID : uint8_t
	{
		AS_MESHLET
	};

	enum MeshShaderID : uint8_t
	{
		MS_MESHLET
	};

	enum PixelShaderID : uint8_t
	{
		PS_MESHLET
	};

	struct ObjectMesh
	{
		XUSG::DescriptorTable SrvTable;
		XUSG::StructuredBuffer::uptr Vertices;
		XUSG::StructuredBuffer::uptr Meshlets;
		XUSG::StructuredBuffer::uptr PrimitiveIndices;
		XUSG::StructuredBuffer::uptr MeshletCullData;
		XUSG::RawBuffer::uptr UniqueVertexIndices;
		XUSG::ConstantBuffer::uptr MeshInfo;
		std::vector<Subset> Subsets;
		uint32_t MeshletCount;
	};

	struct SceneObject
	{
		std::vector<ObjectMesh> Meshes;
		XUSG::ConstantBuffer::uptr Instance;
		uint32_t CbvStride;
		DirectX::XMFLOAT3X4 World;
	};

	bool createMeshBuffers(XUSG::CommandList* pCommandList, ObjectMesh& mesh, const Mesh& meshData, std::vector<XUSG::Resource>& uploaders);
	bool createPayloadBuffers();
	bool createPipelineLayouts(bool isMSSupported);
	bool createPipelines(XUSG::Format rtFormat, XUSG::Format dsFormat, bool isMSSupported);
	bool createCommandLayout();
	bool createDescriptorTables();
	void renderMS(XUSG::Ultimate::CommandList* pCommandList, uint32_t frameIndex);
	void renderFallback(XUSG::CommandList* pCommandList, uint32_t frameIndex);

	XUSG::Device m_device;

	XUSG::PipelineLayout		m_pipelineLayouts[NUM_PIPELINE_LAYOUT];
	XUSG::Pipeline				m_pipelines[NUM_PIPELINE];

	XUSG::CommandLayout			m_commandLayout;

	XUSG::DescriptorTable		m_srvTable;
	XUSG::DescriptorTable		m_uavTable;
	XUSG::DescriptorTable		m_samplerTable;

	XUSG::IndexBuffer::uptr		m_indexPayloads;
	XUSG::StructuredBuffer::uptr m_vertPayloads;
	XUSG::StructuredBuffer::uptr m_dispatchPayloads;

	std::vector<SceneObject>	m_sceneObjects;

	XUSG::DepthStencil::uptr	m_depth;

	XUSG::ConstantBuffer::uptr	m_cbGlobals;
	uint32_t					m_cbvStride;

	XUSG::ShaderPool::uptr					m_shaderPool;
	XUSG::Graphics::PipelineCache::uptr		m_graphicsPipelineCache;
	XUSG::Compute::PipelineCache::uptr		m_computePipelineCache;
	XUSG::MeshShader::PipelineCache::uptr	m_meshShaderPipelineCache;
	XUSG::PipelineLayoutCache::uptr			m_pipelineLayoutCache;
	XUSG::DescriptorTableCache::uptr		m_descriptorTableCache;

	DirectX::XMFLOAT2			m_viewport;
};
