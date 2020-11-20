//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedConst.h"
#include "MeshShaderFallbackLayer.h"

using namespace std;
using namespace XUSG;

MeshShaderFallbackLayer::MeshShaderFallbackLayer(const Device& device, bool isMSSupported) :
	m_device(device),
	m_isMSSupported(isMSSupported),
	m_useNative(isMSSupported)
{
}

MeshShaderFallbackLayer::~MeshShaderFallbackLayer()
{
}

bool MeshShaderFallbackLayer::Init(DescriptorTableCache& descriptorTableCache, uint32_t maxMeshletCount,
	uint32_t groupVertCount, uint32_t groupPrimCount, uint32_t vertexStride, uint32_t batchSize)
{
	// Create command layouts
	N_RETURN(createCommandLayouts(), false);

	// Create payload buffers
	N_RETURN(createPayloadBuffers(maxMeshletCount, groupVertCount, groupPrimCount, vertexStride, batchSize), false);

	// Create descriptor tables
	N_RETURN(createDescriptorTables(descriptorTableCache), false);

	return true;
}

MeshShaderFallbackLayer::PipelineLayout MeshShaderFallbackLayer::GetPipelineLayout(const Util::PipelineLayout::uptr& utilPipelineLayout,
	PipelineLayoutCache& pipelineLayoutCache, PipelineLayoutFlag flags, const wchar_t* name)
{
	PipelineLayout pipelineLayout = {};

	// Native mesh-shader
	if (m_isMSSupported)
		pipelineLayout.m_native = utilPipelineLayout->GetPipelineLayout(pipelineLayoutCache,
			flags, (wstring(name) + L"_NativeLayout").c_str());

	const auto& descriptorTableLayoutKeys = utilPipelineLayout->GetDescriptorTableLayoutKeys();
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
						pipelineLayout->SetRootCBV(index, pRanges->BaseBinding, pRanges->Space, pRanges->Flags, stage);
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

		pipelineLayoutAS->SetRootUAV(pipelineLayout.m_payloadUavIndexAS, 0, FALLBACK_LAYER_PAYLOAD_SPACE); // AS payload buffer
		pipelineLayout.m_fallbacks[FALLBACK_AS] = pipelineLayoutAS->GetPipelineLayout(pipelineLayoutCache,
			flags, (wstring(name) + L"_FallbackASLayout").c_str());
	}

	// Compute-shader fallback for mesh shader
	{
		// Convert the descriptor table layouts of AS to CS
		const auto pipelineLayoutMS = convertDescriptorTableLayouts(Shader::Stage::MS, Shader::Stage::CS, pipelineLayout.m_indexMaps[FALLBACK_MS]);
		pipelineLayout.m_payloadUavIndexMS = static_cast<uint32_t>(pipelineLayoutMS->GetDescriptorTableLayoutKeys().size());
		pipelineLayout.m_payloadSrvIndexMS = pipelineLayout.m_payloadUavIndexMS + 1;
		pipelineLayout.m_batchIndexMS = pipelineLayout.m_payloadSrvIndexMS + 1;

		pipelineLayoutMS->SetRange(pipelineLayout.m_payloadUavIndexMS, DescriptorType::UAV, 2, 0, FALLBACK_LAYER_PAYLOAD_SPACE); // VB and IB payloads
		pipelineLayoutMS->SetRootSRV(pipelineLayout.m_payloadSrvIndexMS, 0, FALLBACK_LAYER_PAYLOAD_SPACE, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE); // AS payload buffer
		pipelineLayoutMS->SetConstants(pipelineLayout.m_batchIndexMS, 1, 0, FALLBACK_LAYER_PAYLOAD_SPACE); // Batch index
		pipelineLayout.m_fallbacks[FALLBACK_MS] = pipelineLayoutMS->GetPipelineLayout(pipelineLayoutCache,
			flags, (wstring(name) + L"_FallbackMSLayout").c_str());
	}

	// Vertex-shader fallback for mesh shader before pixel shader
	{
		auto pipelineLayoutPS = convertDescriptorTableLayouts(Shader::Stage::PS, Shader::Stage::PS, pipelineLayout.m_indexMaps[FALLBACK_PS]);
		pipelineLayout.m_payloadSrvIndexVS = static_cast<uint32_t>(pipelineLayoutPS->GetDescriptorTableLayoutKeys().size());
		pipelineLayout.m_batchIndexVS = pipelineLayout.m_payloadSrvIndexVS + 1;

		pipelineLayoutPS->SetRange(pipelineLayout.m_payloadSrvIndexVS, DescriptorType::SRV, 1, 0, FALLBACK_LAYER_PAYLOAD_SPACE);
		pipelineLayoutPS->SetShaderStage(pipelineLayout.m_payloadSrvIndexVS, Shader::Stage::VS);
		pipelineLayoutPS->SetConstants(pipelineLayout.m_batchIndexVS, 1, 0, FALLBACK_LAYER_PAYLOAD_SPACE, Shader::Stage::VS); // Batch index
		pipelineLayout.m_fallbacks[FALLBACK_PS] = pipelineLayoutPS->GetPipelineLayout(pipelineLayoutCache,
			flags, (wstring(name) + L"_FallbackPSLayout").c_str());
	}

	return pipelineLayout;
}

MeshShaderFallbackLayer::Pipeline MeshShaderFallbackLayer::GetPipeline(const PipelineLayout& pipelineLayout, const Blob& csAS,
	const Blob& csMS, const Blob& vsMS, const MeshShader::State::uptr& state, MeshShader::PipelineCache& meshShaderPipelineCache,
	Compute::PipelineCache& computePipelineCache, Graphics::PipelineCache& graphicsPipelineCache, const wchar_t* name)
{
	Pipeline pipeline = {};

	// Native mesh-shader
	if (m_isMSSupported)
	{
		state->SetPipelineLayout(pipelineLayout.m_native);
		pipeline.m_native = state->GetPipeline(meshShaderPipelineCache, (wstring(name) + L"_Native").c_str());
	}

	// Compute-shader fallback for amplification shader
	if (csAS)
	{
		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(pipelineLayout.m_fallbacks[FALLBACK_AS]);
		state->SetShader(csAS);
		pipeline.m_fallbacks[FALLBACK_AS] = state->GetPipeline(computePipelineCache, (wstring(name) + L"_FallbackAS").c_str());
	}

	// Compute-shader fallback for mesh shader
	assert(csMS);
	{
		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(pipelineLayout.m_fallbacks[FALLBACK_MS]);
		state->SetShader(csMS);
		pipeline.m_fallbacks[FALLBACK_MS] = state->GetPipeline(computePipelineCache, (wstring(name) + L"_FallbackMS").c_str());
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
			void* Blend;
			void* Rasterizer;
			void* DepthStencil;
			PrimitiveTopologyType PrimTopologyType;
			uint8_t	NumRenderTargets;
			Format RTVFormats[8];
			Format	DSVFormat;
			uint8_t	SampleCount;
		};

		const auto& pKey = reinterpret_cast<const Key*>(state->GetKey().c_str());
		const auto gState = Graphics::State::MakeUnique();

		gState->SetPipelineLayout(pipelineLayout.m_fallbacks[FALLBACK_PS]);
		gState->SetShader(Shader::Stage::VS, vsMS);
		if (pKey->Shaders[PS]) gState->SetShader(Shader::Stage::PS, reinterpret_cast<Blob::element_type*>(pKey->Shaders[PS]));

		if (pKey->Blend) gState->OMSetBlendState(make_shared<Graphics::Blend::element_type>(*reinterpret_cast<Graphics::Blend::element_type*>(pKey->Blend)));
		if (pKey->Rasterizer) gState->RSSetState(make_shared<Graphics::Rasterizer::element_type>(*reinterpret_cast<Graphics::Rasterizer::element_type*>(pKey->Rasterizer)));
		if (pKey->DepthStencil) gState->DSSetState(make_shared<Graphics::DepthStencil::element_type>(*reinterpret_cast<Graphics::DepthStencil::element_type*>(pKey->DepthStencil)));
		gState->IASetPrimitiveTopologyType(pKey->PrimTopologyType);

		gState->OMSetNumRenderTargets(pKey->NumRenderTargets);
		for (auto i = 0; i < 8; ++i) gState->OMSetRTVFormat(i, pKey->RTVFormats[i]);
		gState->OMSetDSVFormat(pKey->DSVFormat);

		pipeline.m_fallbacks[FALLBACK_PS] = gState->GetPipeline(graphicsPipelineCache, (wstring(name) + L"_FallbackPS").c_str());
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

void MeshShaderFallbackLayer::SetRootConstantBufferView(CommandList* pCommandList, uint32_t index, const Resource& resource, int offset)
{
	if (m_useNative) pCommandList->SetGraphicsRootConstantBufferView(index, resource, offset);
	else for (auto i = 0u; i < FALLBACK_PIPE_COUNT; ++i)
	{
		const auto& indexPair = m_pCurrentPipelineLayout->m_indexMaps[i][index];
		if (indexPair.Cmd < 0xffffffff)
		{
			auto& commands = m_pipelineSetCommands[i].SetRootCBVs;
			if (commands.size() <= indexPair.Cmd) commands.resize(indexPair.Cmd + 1);
			auto& command = commands[indexPair.Cmd];

			command.Index = indexPair.Prm;
			command.pResource = addressof(resource);
			command.Offset = offset;
		}
	}
}

void MeshShaderFallbackLayer::SetRootShaderResourceView(CommandList* pCommandList, uint32_t index, const Resource& resource, int offset)
{
	if (m_useNative) pCommandList->SetGraphicsRootShaderResourceView(index, resource, offset);
	else for (auto i = 0u; i < FALLBACK_PIPE_COUNT; ++i)
	{
		const auto& indexPair = m_pCurrentPipelineLayout->m_indexMaps[i][index];
		if (indexPair.Cmd < 0xffffffff)
		{
			auto& commands = m_pipelineSetCommands[i].SetRootSRVs;
			if (commands.size() <= indexPair.Cmd) commands.resize(indexPair.Cmd + 1);
			auto& command = commands[indexPair.Cmd];

			command.Index = indexPair.Prm;
			command.pResource = addressof(resource);
			command.Offset = offset;
		}
	}
}

void MeshShaderFallbackLayer::SetRootUnorderedAccessView(CommandList* pCommandList, uint32_t index, const Resource& resource, int offset)
{
	if (m_useNative) pCommandList->SetGraphicsRootUnorderedAccessView(index, resource, offset);
	else for (auto i = 0u; i < FALLBACK_PIPE_COUNT; ++i)
	{
		const auto& indexPair = m_pCurrentPipelineLayout->m_indexMaps[i][index];
		if (indexPair.Cmd < 0xffffffff)
		{
			auto& commands = m_pipelineSetCommands[i].SetRootUAVs;
			if (commands.size() <= indexPair.Cmd) commands.resize(indexPair.Cmd + 1);
			auto& command = commands[indexPair.Cmd];

			command.Index = indexPair.Prm;
			command.pResource = addressof(resource);
			command.Offset = offset;
		}
	}
}

void MeshShaderFallbackLayer::DispatchMesh(Ultimate::CommandList* pCommandList, uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ)
{
	if (m_useNative) pCommandList->DispatchMesh(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
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
				pCommandList->SetComputeRootConstantBufferView(command.Index, *command.pResource, command.Offset);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_AS].SetRootSRVs)
				pCommandList->SetComputeRootShaderResourceView(command.Index, *command.pResource, command.Offset);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_AS].SetRootUAVs)
				pCommandList->SetComputeRootUnorderedAccessView(command.Index, *command.pResource, command.Offset);
			pCommandList->SetComputeRootUnorderedAccessView(m_pCurrentPipelineLayout->m_payloadUavIndexAS, m_dispatchPayloads->GetResource());

			// Set pipeline state
			pCommandList->SetPipelineState(m_pCurrentPipeline->m_fallbacks[FALLBACK_AS]);

			// Record commands.
			pCommandList->Dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		}

		// Set barriers
		numBarriers = m_vertPayloads->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
		numBarriers = m_indexPayloads->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);
		numBarriers = m_dispatchPayloads->SetBarrier(barriers, ResourceState::INDIRECT_ARGUMENT |
			ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
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
				pCommandList->SetComputeRootConstantBufferView(command.Index, *command.pResource, command.Offset);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_MS].SetRootSRVs)
				pCommandList->SetComputeRootShaderResourceView(command.Index, *command.pResource, command.Offset);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_MS].SetRootUAVs)
				pCommandList->SetComputeRootUnorderedAccessView(command.Index, *command.pResource, command.Offset);
			pCommandList->SetComputeDescriptorTable(m_pCurrentPipelineLayout->m_payloadUavIndexMS, m_uavTable);

			// Set pipeline state
			pCommandList->SetPipelineState(m_pCurrentPipeline->m_fallbacks[MeshShaderFallbackLayer::FALLBACK_MS]);

			// Record commands.
			for (auto i = 0u; i < batchCount; ++i)
			{
				const int baseOffset = sizeof(DispatchArgs) * i + sizeof(DispatchArgs::DrawIndexedArgs);
				pCommandList->SetComputeRootShaderResourceView(m_pCurrentPipelineLayout->m_payloadSrvIndexMS, m_dispatchPayloads->GetResource(), baseOffset + sizeof(uint32_t[3]));
				pCommandList->SetCompute32BitConstant(m_pCurrentPipelineLayout->m_batchIndexMS, i);
				pCommandList->ExecuteIndirect(m_commandLayouts[DISPATCH], 1, m_dispatchPayloads->GetResource(), baseOffset);
			}

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
				pCommandList->SetGraphicsRootConstantBufferView(command.Index, *command.pResource, command.Offset);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_PS].SetRootSRVs)
				pCommandList->SetGraphicsRootShaderResourceView(command.Index, *command.pResource, command.Offset);
			for (const auto& command : m_pipelineSetCommands[FALLBACK_PS].SetRootUAVs)
				pCommandList->SetGraphicsRootUnorderedAccessView(command.Index, *command.pResource, command.Offset);
			pCommandList->SetGraphicsDescriptorTable(m_pCurrentPipelineLayout->m_payloadSrvIndexVS, m_srvTable);

			// Set pipeline state
			pCommandList->SetPipelineState(m_pCurrentPipeline->m_fallbacks[MeshShaderFallbackLayer::FALLBACK_PS]);

			// Record commands.
			pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);
			pCommandList->IASetIndexBuffer(m_indexPayloads->GetIBV());
			for (auto i = 0u; i < batchCount; ++i)
			{
				pCommandList->SetGraphics32BitConstant(m_pCurrentPipelineLayout->m_batchIndexVS, i);
				pCommandList->ExecuteIndirect(m_commandLayouts[DRAW_INDEXED], 1, m_dispatchPayloads->GetResource(), sizeof(DispatchArgs) * i);
			}
		}
	}
}

bool MeshShaderFallbackLayer::createPayloadBuffers(uint32_t maxMeshletCount, uint32_t groupVertCount,
	uint32_t groupPrimCount, uint32_t vertexStride, uint32_t batchSize)
{
	{
		m_vertPayloads = StructuredBuffer::MakeUnique();
		N_RETURN(m_vertPayloads->Create(m_device, groupVertCount * maxMeshletCount, vertexStride,
			ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
			1, nullptr, 1, nullptr, L"VertexPayloads"), false);
	}

	{
		m_indexPayloads = IndexBuffer::MakeUnique();
		N_RETURN(m_indexPayloads->Create(m_device, sizeof(uint16_t[3]) * groupPrimCount * maxMeshletCount,
			Format::R16_UINT, ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
			1, nullptr, 1, nullptr, 1, nullptr, L"IndexPayloads"), false);
	}

	{
		const auto batchCount = DIV_UP(maxMeshletCount, batchSize);

		m_dispatchPayloads = StructuredBuffer::MakeUnique();
		const uint32_t stride = sizeof(uint32_t);
		const uint32_t numElements = sizeof(DispatchArgs) / stride * batchCount;
		N_RETURN(m_dispatchPayloads->Create(m_device, numElements, stride,
			ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
			1, nullptr, 1, nullptr, L"DispatchPayloads"), false);
	}

	return true;
}

bool MeshShaderFallbackLayer::createCommandLayouts()
{
	IndirectArgument arg;
	{
		arg.Type = IndirectArgumentType::DISPATCH;
		N_RETURN(m_device->CreateCommandLayout(m_commandLayouts[DISPATCH], sizeof(uint32_t[3]), 1, &arg), false);
	}

	{
		arg.Type = IndirectArgumentType::DRAW_INDEXED;
		N_RETURN(m_device->CreateCommandLayout(m_commandLayouts[DRAW_INDEXED], sizeof(uint32_t[5]), 1, &arg), false);
	}

	return true;
}

bool MeshShaderFallbackLayer::createDescriptorTables(DescriptorTableCache& descriptorTableCache)
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
		X_RETURN(m_uavTable, descriptorTable->GetCbvSrvUavTable(descriptorTableCache), false);
	}

	// Payload SRVs
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_vertPayloads->GetSRV());
		X_RETURN(m_srvTable, descriptorTable->GetCbvSrvUavTable(descriptorTableCache), false);
	}

	return true;
}

bool MeshShaderFallbackLayer::PipelineLayout::IsValid(bool isMSSupported) const
{
	return (m_native != nullptr) == isMSSupported &&
		m_fallbacks[FALLBACK_AS] && m_fallbacks[FALLBACK_MS] && m_fallbacks[FALLBACK_PS];
}

bool MeshShaderFallbackLayer::Pipeline::IsValid(bool isMSSupported) const
{
	return (m_native != nullptr) == isMSSupported && m_fallbacks[FALLBACK_MS] && m_fallbacks[FALLBACK_PS];
}
