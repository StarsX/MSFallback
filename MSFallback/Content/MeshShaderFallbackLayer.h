//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "Ultimate/XUSGUltimate.h"

class MeshShaderFallbackLayer
{
public:
	enum PipelineType
	{
		FALLBACK_AS,
		FALLBACK_MS,
		FALLBACK_PS,

		FALLBACK_PIPE_COUNT
	};

	class PipelineLayout
	{
	public:
		bool IsValid(bool isMSSupported) const;
	//private:
		friend MeshShaderFallbackLayer;
		XUSG::PipelineLayout m_native;
		XUSG::PipelineLayout m_fallbacks[FALLBACK_PIPE_COUNT];

		uint32_t m_payloadUavIndexAS;
		uint32_t m_payloadUavIndexMS;
		uint32_t m_payloadSrvIndexMS;
		uint32_t m_batchIndexMS;
		uint32_t m_payloadSrvIndexVS;
		uint32_t m_batchIndexVS;
	private:
		struct IndexPair
		{
			uint32_t Cmd;
			uint32_t Prm;
		};

		std::vector<IndexPair> m_indexMaps[FALLBACK_PIPE_COUNT];
	};

	class Pipeline
	{
	public:
		bool IsValid(bool isMSSupported) const;
	//private:
		friend MeshShaderFallbackLayer;
		XUSG::Pipeline m_native;
		XUSG::Pipeline m_fallbacks[FALLBACK_PIPE_COUNT];
	};

	MeshShaderFallbackLayer(const XUSG::Device& device, bool isMSSupported);
	virtual ~MeshShaderFallbackLayer();

	bool Init(XUSG::DescriptorTableCache& descriptorTableCache, uint32_t maxMeshletCount,
		uint32_t groupVertCount, uint32_t groupPrimCount, uint32_t vertexStride, uint32_t batchSize);

	PipelineLayout GetPipelineLayout(const XUSG::Util::PipelineLayout::uptr& utilPipelineLayout,
		XUSG::PipelineLayoutCache& pipelineLayoutCache, XUSG::PipelineLayoutFlag flags,
		const wchar_t* name = nullptr);

	Pipeline GetPipeline(const PipelineLayout& pipelineLayout,
		const XUSG::Blob& csAS, const XUSG::Blob& csMS,
		const XUSG::Blob& vsMS, const XUSG::MeshShader::State::uptr& state,
		XUSG::MeshShader::PipelineCache& meshShaderPipelineCache,
		XUSG::Compute::PipelineCache& computePipelineCache,
		XUSG::Graphics::PipelineCache& graphicsPipelineCache,
		const wchar_t* name = nullptr);

	void EnableNativeMeshShader(bool enable);
	void SetPipelineLayout(XUSG::CommandList* pCommandList, const PipelineLayout& pipelineLayout);
	void SetPipelineState(XUSG::CommandList* pCommandList, const Pipeline& pipeline);
	void SetDescriptorTable(XUSG::CommandList* pCommandList, uint32_t index, const XUSG::DescriptorTable& descriptorTable);
	void Set32BitConstant(XUSG::CommandList* pCommandList, uint32_t index, uint32_t srcData, uint32_t destOffsetIn32BitValues = 0);
	void Set32BitConstants(XUSG::CommandList* pCommandList, uint32_t index, uint32_t num32BitValuesToSet,
		const void* pSrcData, uint32_t destOffsetIn32BitValues = 0);
	void SetRootConstantBufferView(XUSG::CommandList* pCommandList, uint32_t index, const XUSG::Resource& resource, int offset = 0);
	void SetRootShaderResourceView(XUSG::CommandList* pCommandList, uint32_t index, const XUSG::Resource& resource, int offset = 0);
	void SetRootUnorderedAccessView(XUSG::CommandList* pCommandList, uint32_t index, const XUSG::Resource& resource, int offset = 0);
	void DispatchMesh(XUSG::Ultimate::CommandList* pCommandList, uint32_t ThreadGroupCountX, uint32_t ThreadGroupCountY, uint32_t ThreadGroupCountZ);

protected:
	enum CommandLayoutType
	{
		DISPATCH,
		DRAW_INDEXED,

		COMMAND_LAYOUT_COUNT
	};

	struct PipelineSetCommands
	{
		struct SetDescriptorTable
		{
			uint32_t Index;
			const XUSG::DescriptorTable* pDescriptorTable;
		};

		struct SetConstants
		{
			uint32_t Index;
			std::vector<uint32_t> Constants;
		};

		struct SetRootView
		{
			uint32_t Index;
			const XUSG::Resource* pResource;
			int Offset;
		};

		std::vector<SetDescriptorTable> SetDescriptorTables;
		std::vector<SetConstants> SetConstants;
		std::vector<SetRootView> SetRootSRVs;
		std::vector<SetRootView> SetRootUAVs;
		std::vector<SetRootView> SetRootCBVs;
	};

	bool createPayloadBuffers(uint32_t maxMeshletCount, uint32_t groupVertCount,
		uint32_t groupPrimCount, uint32_t vertexStride, uint32_t batchSize);
	bool createCommandLayouts();
	bool createDescriptorTables(XUSG::DescriptorTableCache& descriptorTableCache);

	XUSG::Device					m_device;
	XUSG::CommandLayout				m_commandLayouts[COMMAND_LAYOUT_COUNT];

	XUSG::DescriptorTable			m_srvTable;
	XUSG::DescriptorTable			m_uavTable;

	XUSG::IndexBuffer::uptr			m_indexPayloads;
	XUSG::StructuredBuffer::uptr	m_vertPayloads;
	XUSG::StructuredBuffer::uptr	m_dispatchPayloads;

	const PipelineLayout*			m_pCurrentPipelineLayout;
	const Pipeline*					m_pCurrentPipeline;
	PipelineSetCommands				m_pipelineSetCommands[FALLBACK_PIPE_COUNT];

	uint32_t						m_batchIndexCount;

	bool							m_isMSSupported;
	bool							m_useNative;
};
