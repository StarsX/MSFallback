//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "Ultimate/XUSGUltimate.h"
#include "Model.h"

class Renderer
{
public:
	Renderer(const XUSG::Device& device);
	virtual ~Renderer();

	bool Init(XUSG::CommandList* pCommandList, uint32_t width, uint32_t height,
		XUSG::Format rtFormat, std::vector<XUSG::Resource>& uploaders,
		const wchar_t* fileName, const DirectX::XMFLOAT4& posScale, bool isMSSupported);

	void UpdateFrame(uint32_t frameIndex, DirectX::CXMMATRIX view, DirectX::CXMMATRIX proj);
	void Render(XUSG::Ultimate::CommandList* pCommandList, uint32_t frameIndex,
		const XUSG::Descriptor& rtv, bool useMeshShader = true);

	static const uint32_t FrameCount = 3;

protected:
	enum PipelineLayoutIndex : uint8_t
	{
		MESHLET_MS_LAYOUT,
		MESHLET_CS_LAYOUT,
		MESHLET_VS_LAYOUT,

		NUM_PIPELINE_LAYOUT
	};

	enum PipelineLayoutSlot : uint8_t
	{
		CBV_GLOBALS,
		CONSTANTS,
		SRVS,
		UAVS
	};

	enum PipelineIndex : uint8_t
	{
		MESHLET_MS,
		MESHLET_CS,
		MESHLET_VS,

		NUM_PIPELINE
	};

	enum ComputeShaderID : uint8_t
	{
		CS_MESHLET
	};

	enum VertexShaderID : uint8_t
	{
		VS_MESHLET
	};

	enum MeshShaderID : uint8_t
	{
		MS_MESHLET
	};

	enum PixelShaderID : uint8_t
	{
		PS_MESHLET
	};

	struct CBGlobals
	{
		DirectX::XMFLOAT4X4 World;
		DirectX::XMFLOAT4X4 WorldView;
		DirectX::XMFLOAT4X4 WorldViewProj;
		uint32_t DrawMeshlets;
	};

	bool createMeshBuffers(XUSG::CommandList* pCommandList, uint32_t i, const Mesh& mesh, std::vector<XUSG::Resource>& uploaders);
	bool createPayloadBuffers();
	bool createPipelineLayouts(bool isMSSupported);
	bool createPipelines(XUSG::Format rtFormat, XUSG::Format dsFormat, bool isMSSupported);
	bool createDescriptorTables();
	void renderMS(XUSG::Ultimate::CommandList* pCommandList, uint32_t frameIndex);
	void renderFallback(XUSG::CommandList* pCommandList, uint32_t frameIndex);
	void renderVS(XUSG::CommandList* pCommandList, uint32_t frameIndex);

	XUSG::Device m_device;

	XUSG::InputLayout			m_inputLayout;
	XUSG::PipelineLayout		m_pipelineLayouts[NUM_PIPELINE_LAYOUT];
	XUSG::Pipeline				m_pipelines[NUM_PIPELINE];

	std::vector<XUSG::DescriptorTable>	m_srvTables;
	XUSG::DescriptorTable		m_srvTable;
	XUSG::DescriptorTable		m_uavTable;
	XUSG::DescriptorTable		m_samplerTable;

	XUSG::StructuredBuffer::uptr m_vertPayloads;
	XUSG::StructuredBuffer::uptr m_primIdxPayloads;

	std::vector<XUSG::StructuredBuffer::uptr> m_vertices;
	std::vector<XUSG::StructuredBuffer::uptr> m_meshlets;
	std::vector<XUSG::StructuredBuffer::uptr> m_primitiveIndices;
	std::vector<XUSG::RawBuffer::uptr> m_uniqueVertexIndices;

	XUSG::DepthStencil::uptr	m_depth;

	XUSG::ConstantBuffer::uptr	m_cbGlobals;
	uint32_t					m_cbvStride;

	XUSG::ShaderPool::uptr					m_shaderPool;
	XUSG::Graphics::PipelineCache::uptr		m_graphicsPipelineCache;
	XUSG::Compute::PipelineCache::uptr		m_computePipelineCache;
	XUSG::MeshShader::PipelineCache::uptr	m_meshShaderPipelineCache;
	XUSG::PipelineLayoutCache::uptr			m_pipelineLayoutCache;
	XUSG::DescriptorTableCache::uptr		m_descriptorTableCache;

	DirectX::XMFLOAT2				m_viewport;
	DirectX::XMFLOAT4				m_posScale;
	std::vector<std::vector<Subset>> m_subsets;
	std::vector<uint32_t>			m_indexBytes;
};
