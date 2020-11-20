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
	IndirectArgument arg;
	{
		
		arg.Type = IndirectArgumentType::DISPATCH;
		device->CreateCommandLayout(m_commandLayouts[DISPATCH], sizeof(uint32_t[3]), 1, &arg);
	}

	{
		arg.Type = IndirectArgumentType::DRAW_INDEXED;
		device->CreateCommandLayout(m_commandLayouts[DRAW_INDEXED], sizeof(uint32_t[5]), 1, &arg);
	}
}

MeshShaderFallbackLayer::~MeshShaderFallbackLayer()
{
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
						//layout->ranges = DescriptorRangeList(numRanges);
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
			}
		}

		return pipelineLayout;
	};

	// Compute-shader fallback for amplification shader
	{
		// Convert the descriptor table layouts of AS to CS
		const auto pipelineLayoutAS = convertDescriptorTableLayouts(Shader::Stage::AS, Shader::Stage::CS, pipelineLayout.m_indexMaps[FALLBACK_AS]);
		const auto descriptorTableCount = static_cast<uint32_t>(pipelineLayoutAS->GetDescriptorTableLayoutKeys().size());
		pipelineLayoutAS->SetRootUAV(descriptorTableCount, 0, FALLBACK_LAYER_PAYLOAD_SPACE); // AS payload buffer
		pipelineLayout.m_fallbacks[FALLBACK_AS] = pipelineLayoutAS->GetPipelineLayout(pipelineLayoutCache,
			flags, (wstring(name) + L"_FallbackASLayout").c_str());
	}

	// Compute-shader fallback for mesh shader
	{
		// Convert the descriptor table layouts of AS to CS
		const auto pipelineLayoutMS = convertDescriptorTableLayouts(Shader::Stage::MS, Shader::Stage::CS, pipelineLayout.m_indexMaps[FALLBACK_MS]);
		auto descriptorTableCount = static_cast<uint32_t>(pipelineLayoutMS->GetDescriptorTableLayoutKeys().size());
		pipelineLayoutMS->SetRange(descriptorTableCount++, DescriptorType::UAV, 2, 0, FALLBACK_LAYER_PAYLOAD_SPACE); // VB and IB payloads
		pipelineLayoutMS->SetRootSRV(descriptorTableCount++, 0, FALLBACK_LAYER_PAYLOAD_SPACE, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE); // AS payload buffer
		pipelineLayoutMS->SetConstants(descriptorTableCount, 1, 0, FALLBACK_LAYER_PAYLOAD_SPACE); // Batch index
		pipelineLayout.m_fallbacks[FALLBACK_MS] = pipelineLayoutMS->GetPipelineLayout(pipelineLayoutCache,
			flags, (wstring(name) + L"_FallbackMSLayout").c_str());
	}

	// Vertex-shader fallback for mesh shader
	{
		auto pipelineLayoutVS = convertDescriptorTableLayouts(Shader::Stage::PS, Shader::Stage::PS, pipelineLayout.m_indexMaps[FALLBACK_PS]);
		auto descriptorTableCount = static_cast<uint32_t>(pipelineLayoutVS->GetDescriptorTableLayoutKeys().size());
		pipelineLayoutVS->SetRange(descriptorTableCount, DescriptorType::SRV, 1, 0);
		pipelineLayoutVS->SetShaderStage(descriptorTableCount++, Shader::Stage::VS);
		pipelineLayoutVS->SetConstants(descriptorTableCount, 1, 0, FALLBACK_LAYER_PAYLOAD_SPACE, Shader::Stage::VS); // Batch index
		pipelineLayout.m_fallbacks[FALLBACK_PS] = pipelineLayoutVS->GetPipelineLayout(pipelineLayoutCache,
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
		for (auto& commands : m_PipelineSetCommands)
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
		auto& commands = m_PipelineSetCommands[i].SetDescriptorTables;
		if (commands.size() <= indexPair.Cmd) commands.resize(indexPair.Cmd + 1);
		auto& command = commands[indexPair.Cmd];

		command.Index = indexPair.Prm;
		command.pDescriptorTable = &descriptorTable;
	}
}

void MeshShaderFallbackLayer::Set32BitConstant(CommandList* pCommandList, uint32_t index, uint32_t srcData, uint32_t destOffsetIn32BitValues)
{
	if (m_useNative) pCommandList->SetGraphics32BitConstant(index, srcData, destOffsetIn32BitValues);
	else for (auto i = 0u; i < FALLBACK_PIPE_COUNT; ++i)
	{
		const auto& indexPair = m_pCurrentPipelineLayout->m_indexMaps[i][index];
		auto& commands = m_PipelineSetCommands[i].SetConstants;
		if (commands.size() <= indexPair.Cmd) commands.resize(indexPair.Cmd + 1);
		auto& command = commands[indexPair.Cmd];

		command.Index = indexPair.Prm;
		if (command.Constants.size() <= destOffsetIn32BitValues) commands.resize(destOffsetIn32BitValues + 1);
		command.Constants[destOffsetIn32BitValues] = srcData;
	}
}

void MeshShaderFallbackLayer::Set32BitConstants(CommandList* pCommandList, uint32_t index,
	uint32_t num32BitValuesToSet, const void* pSrcData, uint32_t destOffsetIn32BitValues)
{
	if (m_useNative) pCommandList->SetGraphics32BitConstants(index, num32BitValuesToSet, pSrcData, destOffsetIn32BitValues);
	else for (auto i = 0u; i < FALLBACK_PIPE_COUNT; ++i)
	{
		const auto& indexPair = m_pCurrentPipelineLayout->m_indexMaps[i][index];
		auto& commands = m_PipelineSetCommands[i].SetConstants;
		if (commands.size() <= indexPair.Cmd) commands.resize(indexPair.Cmd + 1);
		auto& command = commands[indexPair.Cmd];

		command.Index = indexPair.Prm;
		if (command.Constants.size() < num32BitValuesToSet) commands.resize(num32BitValuesToSet);
		memcpy(command.Constants.data(), &reinterpret_cast<const uint32_t*>(pSrcData)[destOffsetIn32BitValues], sizeof(uint32_t) * num32BitValuesToSet);
	}
}

void MeshShaderFallbackLayer::SetRootConstantBufferView(CommandList* pCommandList, uint32_t index, const Resource& resource, int offset)
{
	if (m_useNative) pCommandList->SetGraphicsRootConstantBufferView(index, resource, offset);
	else for (auto i = 0u; i < FALLBACK_PIPE_COUNT; ++i)
	{
		const auto& indexPair = m_pCurrentPipelineLayout->m_indexMaps[i][index];
		auto& commands = m_PipelineSetCommands[i].SetRootCBVs;
		if (commands.size() <= indexPair.Cmd) commands.resize(indexPair.Cmd + 1);
		auto& command = commands[indexPair.Cmd];

		command.Index = indexPair.Prm;
		command.pResource = addressof(resource);
		command.Offset = offset;
	}
}

void MeshShaderFallbackLayer::SetRootShaderResourceView(CommandList* pCommandList, uint32_t index, const Resource& resource, int offset)
{
	if (m_useNative) pCommandList->SetGraphicsRootShaderResourceView(index, resource, offset);
	else for (auto i = 0u; i < FALLBACK_PIPE_COUNT; ++i)
	{
		const auto& indexPair = m_pCurrentPipelineLayout->m_indexMaps[i][index];
		auto& commands = m_PipelineSetCommands[i].SetRootSRVs;
		if (commands.size() <= indexPair.Cmd) commands.resize(indexPair.Cmd + 1);
		auto& command = commands[indexPair.Cmd];

		command.Index = indexPair.Prm;
		command.pResource = addressof(resource);
		command.Offset = offset;
	}
}

void MeshShaderFallbackLayer::SetRootUnorderedAccessView(CommandList* pCommandList, uint32_t index, const Resource& resource, int offset)
{
	if (m_useNative) pCommandList->SetGraphicsRootUnorderedAccessView(index, resource, offset);
	else for (auto i = 0u; i < FALLBACK_PIPE_COUNT; ++i)
	{
		const auto& indexPair = m_pCurrentPipelineLayout->m_indexMaps[i][index];
		auto& commands = m_PipelineSetCommands[i].SetRootUAVs;
		if (commands.size() <= indexPair.Cmd) commands.resize(indexPair.Cmd + 1);
		auto& command = commands[indexPair.Cmd];

		command.Index = indexPair.Prm;
		command.pResource = addressof(resource);
		command.Offset = offset;
	}
}

void MeshShaderFallbackLayer::DispatchMesh(Ultimate::CommandList* pCommandList, uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ)
{
	if (m_useNative) pCommandList->DispatchMesh(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	{
		// Set barrier
		ResourceBarrier barriers[3];
		//auto numBarriers = m_dispatchPayloads->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);

		const auto batchCount = threadGroupCountX * threadGroupCountY * threadGroupCountZ;

		// Amplification fallback
		if (m_pCurrentPipeline->m_fallbacks[FALLBACK_AS])
		{
			// Set descriptor tables
			pCommandList->SetComputePipelineLayout(m_pCurrentPipelineLayout->m_fallbacks[FALLBACK_AS]);
			for (const auto& command : m_PipelineSetCommands[FALLBACK_AS].SetDescriptorTables)
				pCommandList->SetComputeDescriptorTable(command.Index, *command.pDescriptorTable);
			for (const auto& command : m_PipelineSetCommands[FALLBACK_AS].SetConstants)
				pCommandList->SetCompute32BitConstants(command.Index, static_cast<uint32_t>(command.Constants.size()), command.Constants.data());
			for (const auto& command : m_PipelineSetCommands[FALLBACK_AS].SetRootCBVs)
				pCommandList->SetComputeRootConstantBufferView(command.Index, *command.pResource, command.Offset);
			for (const auto& command : m_PipelineSetCommands[FALLBACK_AS].SetRootSRVs)
				pCommandList->SetComputeRootShaderResourceView(command.Index, *command.pResource, command.Offset);
			for (const auto& command : m_PipelineSetCommands[FALLBACK_AS].SetRootUAVs)
				pCommandList->SetComputeRootUnorderedAccessView(command.Index, *command.pResource, command.Offset);
			//pCommandList->SetComputeRootUnorderedAccessView(UAVS, m_dispatchPayloads->GetResource());

			// Set pipeline state
			pCommandList->SetPipelineState(m_pCurrentPipeline->m_fallbacks[FALLBACK_AS]);

			// Record commands.
			pCommandList->Dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		}
	}
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
