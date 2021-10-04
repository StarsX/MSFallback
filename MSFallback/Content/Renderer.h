//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "MeshShaderFallbackLayer.h"
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

	Renderer(const XUSG::Device::sptr& device);
	virtual ~Renderer();

	bool Init(XUSG::CommandList* pCommandList, uint32_t width, uint32_t height,
		XUSG::Format rtFormat, std::vector<XUSG::Resource::uptr>& uploaders,
		uint32_t objCount, const std::wstring* pFileNames, const ObjectDef* pObjDefs, bool isMSSupported);

	void UpdateFrame(uint8_t frameIndex, DirectX::CXMMATRIX view,
		const DirectX::XMMATRIX* pProj, const DirectX::XMFLOAT3& eyePt);
	void Render(XUSG::Ultimate::CommandList* pCommandList, uint8_t frameIndex,
		const XUSG::Descriptor& rtv, bool useMeshShader = true);

	static const uint8_t FrameCount = 3;

protected:
	enum PipelineLayoutSlot : uint8_t
	{
		CBV_GLOBALS,
		CBV_MESHINFO,
		CBV_INSTANCE,
		SRV_INPUTS,
		SRV_CULL
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
		DirectX::XMFLOAT3X4 World;
	};

	bool createMeshBuffers(XUSG::CommandList* pCommandList, ObjectMesh& mesh,
		const Mesh& meshData, std::vector<XUSG::Resource::uptr>& uploaders);
	bool createPipelineLayouts(bool isMSSupported);
	bool createPipelines(XUSG::Format rtFormat, XUSG::Format dsFormat, bool isMSSupported);
	bool createDescriptorTables();

	XUSG::Device::sptr m_device;

	std::vector<SceneObject>	m_sceneObjects;

	XUSG::DepthStencil::uptr	m_depth;

	XUSG::ConstantBuffer::uptr	m_cbGlobals;

	XUSG::ShaderPool::uptr					m_shaderPool;
	XUSG::Graphics::PipelineCache::uptr		m_graphicsPipelineCache;
	XUSG::Compute::PipelineCache::uptr		m_computePipelineCache;
	XUSG::MeshShader::PipelineCache::uptr	m_meshShaderPipelineCache;
	XUSG::PipelineLayoutCache::uptr			m_pipelineLayoutCache;
	XUSG::DescriptorTableCache::uptr		m_descriptorTableCache;

	std::unique_ptr<MeshShaderFallbackLayer> m_meshShaderFallbackLayer;
	MeshShaderFallbackLayer::PipelineLayout m_pipelineLayout;
	MeshShaderFallbackLayer::Pipeline m_pipeline;

	DirectX::XMFLOAT2 m_viewport;
};
