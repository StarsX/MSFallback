//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedConst.h"
#include "MeshShaderFallbackLayer.h"

using namespace std;
using namespace XUSG;

MeshShaderFallbackLayer::MeshShaderFallbackLayer(bool isMSSupported) :
	m_isMSSupported(isMSSupported),
	m_useNative(isMSSupported)
{
}

MeshShaderFallbackLayer::~MeshShaderFallbackLayer()
{
}

bool MeshShaderFallbackLayer::Init(const Device* pDevice, DescriptorTableLib* pDescriptorTableLib, uint32_t maxMeshletCount,
	uint32_t groupVertCount, uint32_t groupPrimCount, uint32_t vertexStride, uint32_t batchSize)
{
	// Create payload buffers
	XUSG_N_RETURN(createPayloadBuffers(pDevice, maxMeshletCount, groupVertCount, groupPrimCount, vertexStride, batchSize), false);

	// Create descriptor tables
	XUSG_N_RETURN(createDescriptorTables(pDescriptorTableLib), false);

	return true;
}

MeshShaderFallbackLayer::PipelineLayout MeshShaderFallbackLayer::GetPipelineLayout(const Device* pDevice, Util::PipelineLayout* pUtilPipelineLayout,
	PipelineLayoutLib* pPipelineLayoutCache, PipelineLayoutFlag flags, const wchar_t* name)
{
	PipelineLayout pipelineLayout = {};

	// Native mesh-shader
	if (m_isMSSupported)
		pipelineLayout.m_native = pUtilPipelineLayout->GetPipelineLayout(pPipelineLayoutCache,
			flags, (wstring(name) + L"_NativeLayout").c_str());

	const auto& descriptorTableLayoutKeys = pUtilPipelineLayout->GetDescriptorTableLayoutKeys();
	const auto convertDescriptorTableLayouts = [&descriptorTableLayoutKeys](Shader::Stage srcStage, Shader::Stage dstStage, vector<PipelineLayout::IndexPair>& indexMaps)
	{
		struct DescriptorRange
		{
			DescriptorType ViewType;
			uint32_t NumDescriptors;
			uint32_t BaseBinding;
			uint32_t Space;
			DescriptorFlag Flags;
		};

		const auto pipelineLayout = Util::PipelineLayout::MakeShared();
		const auto descriptorTableCount = static_cast<uint32_t>(descriptorTableLayoutKeys.size());

		auto index = 0u, constIndex = 0u, srvIndex = 0u, uavIndex = 0u, cbvIndex = 0u, descTableIndex = 0u;
		indexMaps.resize(descriptorTableCount);

		for (auto n = 0u; n < descriptorTableCount; ++n)
		{
			const auto& key = descriptorTableLayoutKeys[n];
			const auto numRanges = key.size() > 0 ? static_cast<uint32_t>((key.size() - 1) / sizeof(DescriptorRange)) : 0;

			if (numRanges > 0)
			{
				auto stage = static_cast<Shader::Stage>(key[0]);
				if (stage == srcStage || stage == Shader::Stage::ALL)
				{
					const auto pRanges = reinterpret_cast<const DescriptorRange*>(&key[1]);
					stage = stage != Shader::Stage::ALL ? dstStage : stage;

					switch (pRanges->ViewType)
					{
					case DescriptorType::CONSTANT:
						pipelineLayout->SetConstants(index, pRanges->NumDescriptors, pRanges->BaseBinding, pRanges->Space, stage);
						indexMaps[n] = { constIndex++, index };
						break;
					case DescriptorType::ROOT_SRV:
						pipelineLayout->SetRootSRV(index, pRanges->BaseBinding, pRanges->Space, pRanges->Flags, stage);
						indexMaps[n] = { srvIndex++, index };
						break;
					case DescriptorType::ROOT_UAV:
						pipelineLayout->SetRootUAV(index, pRanges->BaseBinding, pRanges->Space, pRanges->Flags, stage);
						indexMaps[n] = { uavIndex++, index };
						break;
					case DescriptorType::ROOT_CBV:
						pipelineLayout->SetRootCBV(index, pRanges->BaseBinding, pRanges->Space, stage);
						indexMaps[n] = { cbvIndex++, index };
						break;
					default:
						for (auto i = 0u; i < numRanges; ++i)
						{
							const auto& range = pRanges[i];
							pipelineLayout->SetRange(index, range.ViewType, range.NumDescriptors, range.BaseBinding, range.Space, range.Flags);
						}
						pipelineLayout->SetShaderStage(index, stage);
						indexMaps[n] = { descTableIndex++, index };
					}

					++index;
				}
				else indexMaps[n] = { 0xffffffff };
			}
		}

		return pipelineLayout;
	};

	// Compute-shader fallback for amplification shader
	{
		// Convert the descriptor table layouts of AS to CS
		const auto pipelineLayoutAS = convertDescriptorTableLayouts(Shader::Stage::AS, Shader::Stage::CS, pipelineLayout.m_indexMaps[FALLBACK_AS]);
		pipelineLayout.m_payloadUavIndexAS = static_cast<uint32_t>(pipelineLayoutAS->GetDescriptorTableLayoutKeys().size());

		pipelineLayoutAS->SetRootUAV(pipelineLayout.m_payloadUavIndexAS, 0, FALLBACK_LAYER_PAYLOAD_SPACE,
			DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE | DescriptorFlag::DESCRIPTORS_VOLATILE); // AS payload buffer
		pipelineLayout.m_fallbacks[FALLBACK_AS] = pipelineLayoutAS->GetPipelineLayout(pPipelineLayoutCache,
			flags, (wstring(name) + L"_FallbackASLayout").c_str());
	}

	// Compute-shader fallback for mesh shader
	uint32_t batchIndexMS;
	{
		// Convert the descriptor table layouts of AS to CS
		const auto pipelineLayoutMS = convertDescriptorTableLayouts(Shader::Stage::MS, Shader::Stage::CS, pipelineLayout.m_indexMaps[FALLBACK_MS]);
		pipelineLayout.m_payloadUavIndexMS = static_cast<uint32_t>(pipelineLayoutMS->GetDescriptorTableLayoutKeys().size());
		pipelineLayout.m_payloadSrvIndexMS = pipelineLayout.m_payloadUavIndexMS + 1;
		batchIndexMS = pipelineLayout.m_payloadSrvIndexMS + 1;

		pipelineLayoutMS->SetRange(pipelineLayout.m_payloadUavIndexMS, DescriptorType::UAV, 2, 0,
			FALLBACK_LAYER_PAYLOAD_SPACE, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE); // VB and IB payloads
		pipelineLayoutMS->SetRootSRV(pipelineLayout.m_payloadSrvIndexMS, 0, FALLBACK_LAYER_PAYLOAD_SPACE); // AS payload buffer
		pipelineLayoutMS->SetConstants(batchIndexMS, 1, 0, FALLBACK_LAYER_PAYLOAD_SPACE); // Batch index
		pipelineLayout.m_fallbacks[FALLBACK_MS] = pipelineLayoutMS->GetPipelineLayout(pPipelineLayoutCache,
			flags, (wstring(name) + L"_FallbackMSLayout").c_str());
	}

	// Vertex-shader fallback for mesh shader before pixel shader
	uint32_t batchIndexVS;
	{
		auto pipelineLayoutPS = convertDescriptorTableLayouts(Shader::Stage::PS, Shader::Stage::PS, pipelineLayout.m_indexMaps[FALLBACK_PS]);
		pipelineLayout.m_payloadSrvIndexVS = static_cast<uint32_t>(pipelineLayoutPS->GetDescriptorTableLayoutKeys().size());
		batchIndexVS = pipelineLayout.m_payloadSrvIndexVS + 1;

		pipelineLayoutPS->SetRange(pipelineLayout.m_payloadSrvIndexVS, DescriptorType::SRV, 1, 0,
			FALLBACK_LAYER_PAYLOAD_SPACE, DescriptorFlag::DESCRIPTORS_VOLATILE);
		pipelineLayoutPS->SetShaderStage(pipelineLayout.m_payloadSrvIndexVS, Shader::Stage::VS);
		pipelineLayoutPS->SetConstants(batchIndexVS, 1, 0, FALLBACK_LAYER_PAYLOAD_SPACE, Shader::Stage::VS); // Batch index
		pipelineLayout.m_fallbacks[FALLBACK_PS] = pipelineLayoutPS->GetPipelineLayout(pPipelineLayoutCache,
			flags, (wstring(name) + L"_FallbackPSLayout").c_str());
	}

	pipelineLayout.CreateCommandLayouts(pDevice, batchIndexMS, batchIndexVS);

	return pipelineLayout;
}

MeshShaderFallbackLayer::Pipeline MeshShaderFallbackLayer::GetPipeline(const PipelineLayout& pipelineLayout, const Blob& csAS,
	const Blob& csMS, const Blob& vsMS, MeshShader::State* pState, MeshShader::PipelineLib* pMeshShaderPipelineLib,
	Compute::PipelineLib* pComputePipelineLib, Graphics::PipelineLib* pGraphicsPipelineLib, const wchar_t* name)
{
	Pipeline pipeline = {};

	// Native mesh-shader
	if (m_isMSSupported)
	{
		pState->SetPipelineLayout(pipelineLayout.m_native);
		pipeline.m_native = pState->GetPipeline(pMeshShaderPipelineLib, (wstring(name) + L"_Native").c_str());
	}

	// Compute-shader fallback for amplification shader
	if (csAS)
	{
		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(pipelineLayout.m_fallbacks[FALLBACK_AS]);
		state->SetShader(csAS);
		pipeline.m_fallbacks[FALLBACK_AS] = state->GetPipeline(pComputePipelineLib, (wstring(name) + L"_FallbackAS").c_str());
	}

	// Compute-shader fallback for mesh shader
	assert(csMS);
	{
		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(pipelineLayout.m_fallbacks[FALLBACK_MS]);
		state->SetShader(csMS);
		pipeline.m_fallbacks[FALLBACK_MS] = state->GetPipeline(pComputePipelineLib, (wstring(name) + L"_FallbackMS").c_str());
	}

	// Vertex-shader fallback for mesh shader
	assert(vsMS);
	{
		enum Stage
		{
			PS,
			MS,
			AS,

			NUM_STAGE
		};

		struct Key
		{
			void* PipelineLayout;
			void* Shaders[NUM_STAGE];
			const Graphics::Blend* pBlend;
			const Graphics::Rasterizer* pRasterizer;
			const Graphics::DepthStencil* pDepthStencil;
			const void* pCachedBlob;
			size_t CachedBlobSize;
			PrimitiveTopologyType PrimTopologyType;
			uint8_t	NumRenderTargets;
			Format RTVFormats[8];
			Format	DSVFormat;
			uint8_t	SampleCount;
			uint8_t SampleQuality;
			uint32_t SampleMask;
			uint32_t NodeMask;
		};

		const auto& pKey = reinterpret_cast<const Key*>(pState->GetKey().c_str());
		const auto gState = Graphics::State::MakeUnique();

		gState->SetPipelineLayout(pipelineLayout.m_fallbacks[FALLBACK_PS]);
		gState->SetShader(Shader::Stage::VS, vsMS);
		if (pKey->Shaders[PS]) gState->SetShader(Shader::Stage::PS, pKey->Shaders[PS]);

		if (pKey->pBlend) gState->OMSetBlendState(pKey->pBlend, pKey->SampleMask);
		if (pKey->pRasterizer) gState->RSSetState(pKey->pRasterizer);
		if (pKey->pDepthStencil) gState->DSSetState(pKey->pDepthStencil);
		if (pKey->pCachedBlob) gState->SetCachedPipeline(pKey->pCachedBlob, pKey->CachedBlobSize);
		gState->IASetPrimitiveTopologyType(pKey->PrimTopologyType);

		gState->OMSetNumRenderTargets(pKey->NumRenderTargets);
		for (auto i = 0; i < 8; ++i) gState->OMSetRTVFormat(i, pKey->RTVFormats[i]);
		gState->OMSetDSVFormat(pKey->DSVFormat);

		gState->OMSetSample(pKey->SampleCount, pKey->SampleQuality);

		pipeline.m_fallbacks[FALLBACK_PS] = gState->GetPipeline(pGraphicsPipelineLib, (wstring(name) + L"_FallbackPS").c_str());
	}

	return pipeline;
}

void MeshShaderFallbackLayer::EnableNativeMeshShader(bool enable)
{
	m_useNative = enable && m_isMSSupported;
}

void MeshShaderFallbackLayer::SetPipelineLayout(CommandList* pCommandList, const PipelineLayout& pipelineLayout)
{
	if (m_useNative) pCommandList->SetGraphicsPipelineLayout(pipelineLayout.m_native);
	else
	{
		for (auto& commands : m_pipelineSetCommands)
		{
			commands.SetDescriptorTables.clear();
			commands.SetConstants.clear();
			commands.SetRootCBVs.clear();
			commands.SetRootSRVs.clear();
			commands.SetRootUAVs.clear();
		}

		m_pCurrentPipelineLayout = &pipelineLayout;
	}
}

void MeshShaderFallbackLayer::SetPipelineState(CommandList* pCommandList, const Pipeline& pipeline)
{
	if (m_useNative) pCommandList->SetPipelineState(pipeline.m_native);
	else m_pCurrentPipeline = &pipeline;
}

void MeshShaderFallbackLayer::SetDescriptorTable(CommandList* pCommandList, uint32_t index, const DescriptorTable& descriptorTable)
{
	if (m_useNative) pCommandList->SetGraphicsDescriptorTable(index, descriptorTable);
	else for(auto i = 0u; i < FALLBACK_PIPE_COUNT; ++i)
	{
		const auto& indexPair = m_pCurrentPipelineLayout->m_indexMaps[i][index];
		if (indexPair.Cmd < 0xffffffff)
		{
			auto& commands = m_pipelineSetCommands[i].SetDescriptorTables;
			if (commands.size() <= indexPair.Cmd) commands.resize(indexPair.Cmd + 1);
			auto& command = commands[indexPair.Cmd];

			command.Index = indexPair.Prm;
			command.pDescriptorTable = &descriptorTable;
		}
	}
}

void MeshShaderFallbackLayer::Set32BitConstant(CommandList* pCommandList, uint32_t index, uint32_t srcData, uint32_t destOffsetIn32BitValues)
{
	if (m_useNative) pCommandList->SetGraphics32BitConstant(index, srcData, destOffsetIn32BitValues);
	else for (auto i = 0u; i < FALLBACK_PIPE_COUNT; ++i)
	{
		const auto& indexPair = m_pCurrentPipelineLayout->m_indexMaps[i][index];
		if (indexPair.Cmd < 0xffffffff)
		{
			auto& commands = m_pipelineSetCommands[i].SetConstants;
			if (commands.size() <= indexPair.Cmd) commands.resize(indexPair.Cmd + 1);
			auto& command = commands[indexPair.Cmd];

			command.Index = indexPair.Prm;
			if (command.Constants.size() <= destOffsetIn32BitValues) commands.resize(destOffsetIn32BitValues + 1);
			command.Constants[destOffsetIn32BitValues] = srcData;
		}
	}
}

void MeshShaderFallbackLayer::Set32BitConstants(CommandList* pCommandList, uint32_t index,
	uint32_t num32BitValuesToSet, const void* pSrcData, uint32_t destOffsetIn32BitValues)
{
	if (m_useNative) pCommandList->SetGraphics32BitConstants(index, num32BitValuesToSet, pSrcData, destOffsetIn32BitValues);
	else for (auto i = 0u; i < FALLBACK_PIPE_COUNT; ++i)
	{
		const auto& indexPair = m_pCurrentPipelineLayout->m_indexMaps[i][index];
		if (indexPair.Cmd < 0xffffffff)
		{
			auto& commands = m_pipelineSetCommands[i].SetConstants;
			if (commands.size() <= indexPair.Cmd) commands.resize(indexPair.Cmd + 1);
			auto& command = commands[indexPair.Cmd];

			command.Index = indexPair.Prm;
			if (command.Constants.size() < num32BitValuesToSet) commands.resize(num32BitValuesToSet);
			memcpy(command.Constants.data(), &reinterpret_cast<const uint32_t*>(pSrcData)[destOffsetIn32BitValues], sizeof(uint32_t) * num32BitValuesToSet);
		}
	}
}

void MeshShaderFallbackLayer::SetRootConstantBufferView(CommandList* pCommandList, uint32_t index, const Resource* pResource, int offset)
{
	if (m_useNative) pCommandList->SetGraphicsRootConstantBufferView(index, pResource, offset);
	else for (auto i = 0u; i < FALLBACK_PIPE_COUNT; ++i)
	{
		const auto& indexPair = m_pCurrentPipelineLayout->m_indexMaps[i][index];
		if (indexPair.Cmd < 0xffffffff)
		{
			auto& commands = m_pipelineSetCommands[i].SetRootCBVs;
			if (commands.size() <= indexPair.Cmd) commands.resize(indexPair.Cmd + 1);
			auto& command = commands[indexPair.Cmd];

			command.Index = indexPair.Prm;
			command.pResource = pResource;
			command.Offset = offset;
		}
	}
}

void MeshShaderFallbackLayer::SetRootShaderResourceView(CommandList* pCommandList, uint32_t index, const Resource* pResource, int offset)
{
	if (m_useNative) pCommandList->SetGraphicsRootShaderResourceView(index, pResource, offset);
	else for (auto i = 0u; i < FALLBACK_PIPE_COUNT; ++i)
	{
		const auto& indexPair = m_pCurrentPipelineLayout->m_indexMaps[i][index];
		if (indexPair.Cmd < 0xffffffff)
		{
			auto& commands = m_pipelineSetCommands[i].SetRootSRVs;
			if (commands.size() <= indexPair.Cmd) commands.resize(indexPair.Cmd + 1);
			auto& command = commands[indexPair.Cmd];

			command.Index = indexPair.Prm;
			command.pResource = pResource;
			command.Offset = offset;
		}
	}
}

void MeshShaderFallbackLayer::SetRootUnorderedAccessView(CommandList* pCommandList, uint32_t index, const Resource* pResource, int offset)
{
	if (m_useNative) pCommandList->SetGraphicsRootUnorderedAccessView(index, pResource, offset);
	else for (auto i = 0u; i < FALLBACK_PIPE_COUNT; ++i)
	{
		const auto& indexPair = m_pCurrentPipelineLayout->m_indexMaps[i][index];
		if (indexPair.Cmd < 0xffffffff)
		{
			auto& commands = m_pipelineSetCommands[i].SetRootUAVs;
			if (commands.size() <= indexPair.Cmd) commands.resize(indexPair.Cmd + 1);
			auto& command = commands[indexPair.Cmd];

			command.Index = indexPair.Prm;
			command.pResource = pResource;
			command.Offset = offset;
		}
	}
}

void MeshShaderFallbackLayer::DispatchMesh(Ultimate::CommandList* pCommandList, uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ)
{
	if (m_useNative) pCommandList->DispatchMesh(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	else
	{
		// Set barrier
		ResourceBarrier barriers[3];
		auto numBarriers = m_dispatchPayloads->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);

		const auto batchCount = threadGroupCountX * threadGroupCountY * threadGroupCountZ;

		// Amplification fallback
		if (m_pCurrentPipeline->m_fallbacks[FALLBACK_AS])
		{
			// Set descriptor tables
			pCommandList->SetComputePipelineLayout(m_pCurrentPipelineLayout->m_fallbacks[FALLBACK_AS]);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_AS].SetDescriptorTables)
				pCommandList->SetComputeDescriptorTable(command.Index, *command.pDescriptorTable);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_AS].SetConstants)
				pCommandList->SetCompute32BitConstants(command.Index, static_cast<uint32_t>(command.Constants.size()), command.Constants.data());
			for (const auto& command : m_pipelineSetCommands[FALLBACK_AS].SetRootCBVs)
				pCommandList->SetComputeRootConstantBufferView(command.Index, command.pResource, command.Offset);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_AS].SetRootSRVs)
				pCommandList->SetComputeRootShaderResourceView(command.Index, command.pResource, command.Offset);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_AS].SetRootUAVs)
				pCommandList->SetComputeRootUnorderedAccessView(command.Index, command.pResource, command.Offset);
			pCommandList->SetComputeRootUnorderedAccessView(m_pCurrentPipelineLayout->m_payloadUavIndexAS, m_dispatchPayloads.get());

			// Set pipeline state
			pCommandList->SetPipelineState(m_pCurrentPipeline->m_fallbacks[FALLBACK_AS]);

			// Record commands.
			pCommandList->Dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		}

		// Set barriers
		numBarriers = m_vertPayloads->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS,
			0, XUSG_BARRIER_ALL_SUBRESOURCES, BarrierFlag::RESET_SRC_STATE);
		m_indexPayloads->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS,
			numBarriers, XUSG_BARRIER_ALL_SUBRESOURCES, BarrierFlag::RESET_SRC_STATE);
		numBarriers = m_dispatchPayloads->SetBarrier(barriers, ResourceState::INDIRECT_ARGUMENT |
			ResourceState::NON_PIXEL_SHADER_RESOURCE);
		pCommandList->Barrier(numBarriers, barriers);

		// Mesh-shader fallback
		{
			// Set descriptor tables
			pCommandList->SetComputePipelineLayout(m_pCurrentPipelineLayout->m_fallbacks[FALLBACK_MS]);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_MS].SetDescriptorTables)
				pCommandList->SetComputeDescriptorTable(command.Index, *command.pDescriptorTable);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_MS].SetConstants)
				pCommandList->SetCompute32BitConstants(command.Index, static_cast<uint32_t>(command.Constants.size()), command.Constants.data());
			for (const auto& command : m_pipelineSetCommands[FALLBACK_MS].SetRootCBVs)
				pCommandList->SetComputeRootConstantBufferView(command.Index, command.pResource, command.Offset);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_MS].SetRootSRVs)
				pCommandList->SetComputeRootShaderResourceView(command.Index, command.pResource, command.Offset);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_MS].SetRootUAVs)
				pCommandList->SetComputeRootUnorderedAccessView(command.Index, command.pResource, command.Offset);
			pCommandList->SetComputeDescriptorTable(m_pCurrentPipelineLayout->m_payloadUavIndexMS, m_uavTable);
			pCommandList->SetComputeRootShaderResourceView(m_pCurrentPipelineLayout->m_payloadSrvIndexMS,
				m_dispatchPayloads.get(), offsetof(DispatchArgs, ASDispatchArgs.MeshletIndices));

			// Set pipeline state
			pCommandList->SetPipelineState(m_pCurrentPipeline->m_fallbacks[MeshShaderFallbackLayer::FALLBACK_MS]);

			// Record commands.
			pCommandList->ExecuteIndirect(m_pCurrentPipelineLayout->GetCommandLayout(DISPATCH),
				batchCount, m_dispatchPayloads.get(), offsetof(DispatchArgs, ASDispatchArgs));

			// Set barriers
			numBarriers = m_vertPayloads->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE);
			numBarriers = m_indexPayloads->SetBarrier(barriers, ResourceState::INDEX_BUFFER, numBarriers);
			pCommandList->Barrier(numBarriers, barriers);

			// Set descriptor tables
			pCommandList->SetGraphicsPipelineLayout(m_pCurrentPipelineLayout->m_fallbacks[MeshShaderFallbackLayer::FALLBACK_PS]);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_PS].SetDescriptorTables)
				pCommandList->SetGraphicsDescriptorTable(command.Index, *command.pDescriptorTable);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_PS].SetConstants)
				pCommandList->SetGraphics32BitConstants(command.Index, static_cast<uint32_t>(command.Constants.size()), command.Constants.data());
			for (const auto& command : m_pipelineSetCommands[FALLBACK_PS].SetRootCBVs)
				pCommandList->SetGraphicsRootConstantBufferView(command.Index, command.pResource, command.Offset);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_PS].SetRootSRVs)
				pCommandList->SetGraphicsRootShaderResourceView(command.Index, command.pResource, command.Offset);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_PS].SetRootUAVs)
				pCommandList->SetGraphicsRootUnorderedAccessView(command.Index, command.pResource, command.Offset);
			pCommandList->SetGraphicsDescriptorTable(m_pCurrentPipelineLayout->m_payloadSrvIndexVS, m_srvTable);

			// Set pipeline state
			pCommandList->SetPipelineState(m_pCurrentPipeline->m_fallbacks[MeshShaderFallbackLayer::FALLBACK_PS]);

			// Record commands.
			pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);
			pCommandList->IASetIndexBuffer(m_indexPayloads->GetIBV());
			pCommandList->ExecuteIndirect(m_pCurrentPipelineLayout->GetCommandLayout(DRAW_INDEXED), batchCount, m_dispatchPayloads.get());
		}
	}
}

bool MeshShaderFallbackLayer::createPayloadBuffers(const Device* pDevice, uint32_t maxMeshletCount, uint32_t groupVertCount,
	uint32_t groupPrimCount, uint32_t vertexStride, uint32_t batchSize)
{
	{
		m_vertPayloads = StructuredBuffer::MakeUnique();
		XUSG_N_RETURN(m_vertPayloads->Create(pDevice, groupVertCount * maxMeshletCount, vertexStride,
			ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
			1, nullptr, 1, nullptr, MemoryFlag::NONE, L"VertexPayloads"), false);
	}

	{
		m_indexPayloads = IndexBuffer::MakeUnique();
		XUSG_N_RETURN(m_indexPayloads->Create(pDevice, sizeof(uint16_t[3]) * groupPrimCount * maxMeshletCount,
			Format::R16_UINT, ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
			1, nullptr, 1, nullptr, 1, nullptr, MemoryFlag::NONE, L"IndexPayloads"), false);
	}

	{
		const auto batchCount = XUSG_DIV_UP(maxMeshletCount, batchSize);

		m_dispatchPayloads = StructuredBuffer::MakeUnique();
		uint32_t numElements = XUSG_UINT32_SIZE_OF(DispatchArgs) * batchCount;
		numElements += XUSG_UINT32_SIZE_OF(DispatchArgs::DrawIndexedArgs); // To avoid overflow
		XUSG_N_RETURN(m_dispatchPayloads->Create(pDevice, numElements, sizeof(uint32_t),
			ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
			1, nullptr, 1, nullptr, MemoryFlag::NONE, L"DispatchPayloads"), false);
	}

	return true;
}

bool MeshShaderFallbackLayer::createDescriptorTables(DescriptorTableLib* pDescriptorTableLib)
{
	// Payload UAVs
	{
		const Descriptor descriptors[] =
		{
			m_vertPayloads->GetUAV(),
			m_indexPayloads->GetUAV()
		};
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		XUSG_X_RETURN(m_uavTable, descriptorTable->GetCbvSrvUavTable(pDescriptorTableLib), false);
	}

	// Payload SRVs
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_vertPayloads->GetSRV());
		XUSG_X_RETURN(m_srvTable, descriptorTable->GetCbvSrvUavTable(pDescriptorTableLib), false);
	}

	return true;
}

void MeshShaderFallbackLayer::PipelineLayout::CreateCommandLayouts(const Device* pDevice, uint32_t batchIndexMS, uint32_t batchIndexVS)
{
	IndirectArgument args[2];
	args[0].Type = IndirectArgumentType::CONSTANT;
	args[0].Constant.Num32BitValuesToSet = 1;
	args[0].Constant.DestOffsetIn32BitValues = 0;
	{
		args[0].Constant.Index = batchIndexMS;
		args[1].Type = IndirectArgumentType::DISPATCH;
		m_commandLayouts[DISPATCH] = CommandLayout::MakeUnique();
		XUSG_N_RETURN(m_commandLayouts[DISPATCH]->Create(pDevice, sizeof(DispatchArgs),
			static_cast<uint32_t>(size(args)), args, m_fallbacks[FALLBACK_MS]), void());
	}

	{
		args[0].Constant.Index = batchIndexVS;
		args[1].Type = IndirectArgumentType::DRAW_INDEXED;
		m_commandLayouts[DRAW_INDEXED] = CommandLayout::MakeUnique();
		XUSG_N_RETURN(m_commandLayouts[DRAW_INDEXED]->Create(pDevice, sizeof(DispatchArgs),
			static_cast<uint32_t>(size(args)), args, m_fallbacks[FALLBACK_PS]), void());
	}
}

bool MeshShaderFallbackLayer::PipelineLayout::IsValid(bool isMSSupported) const
{
	return (m_native != nullptr) == isMSSupported &&
		m_fallbacks[FALLBACK_AS] && m_fallbacks[FALLBACK_MS] && m_fallbacks[FALLBACK_PS];
}

const CommandLayout* MeshShaderFallbackLayer::PipelineLayout::GetCommandLayout(CommandLayoutType type) const
{
	return m_commandLayouts[type].get();
}

bool MeshShaderFallbackLayer::Pipeline::IsValid(bool isMSSupported) const
{
	return (m_native != nullptr) == isMSSupported && m_fallbacks[FALLBACK_MS] && m_fallbacks[FALLBACK_PS];
}
