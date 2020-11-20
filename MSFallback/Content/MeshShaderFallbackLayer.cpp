//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedConst.h"
#include "MeshShaderFallbackLayer.h"

using namespace std;
using namespace XUSG;

MeshShaderFallbackLayer::MeshShaderFallbackLayer(const Device& device, bool isMSSupported) :
	m_device(device),
	m_isMSSupported(isMSSupported)
{
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
	const auto convertDescriptorTableLayouts = [&descriptorTableLayoutKeys](Shader::Stage srcStage, Shader::Stage dstStage)
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

		auto index = 0u;
		for (const auto& key : descriptorTableLayoutKeys)
		{
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
						break;
					case DescriptorType::ROOT_SRV:
						pipelineLayout->SetRootSRV(index, pRanges->BaseBinding, pRanges->Space, pRanges->Flags, stage);
						break;
					case DescriptorType::ROOT_UAV:
						pipelineLayout->SetRootUAV(index, pRanges->BaseBinding, pRanges->Space, pRanges->Flags, stage);
						break;
					case DescriptorType::ROOT_CBV:
						pipelineLayout->SetRootCBV(index, pRanges->BaseBinding, pRanges->Space, pRanges->Flags, stage);
						break;
					default:
						//layout->ranges = DescriptorRangeList(numRanges);
						for (auto i = 0u; i < numRanges; ++i)
						{
							const auto& range = pRanges[i];
							pipelineLayout->SetRange(index, range.ViewType, range.NumDescriptors, range.BaseBinding, range.Space, range.Flags);
						}
						pipelineLayout->SetShaderStage(index, stage);
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
		const auto pipelineLayoutAS = convertDescriptorTableLayouts(Shader::Stage::AS, Shader::Stage::CS);
		const auto descriptorTableCount = static_cast<uint32_t>(pipelineLayoutAS->GetDescriptorTableLayoutKeys().size());
		pipelineLayoutAS->SetRootUAV(descriptorTableCount, 0, FALLBACK_LAYER_PAYLOAD_SPACE); // AS payload buffer
		pipelineLayout.m_as = pipelineLayoutAS->GetPipelineLayout(pipelineLayoutCache,
			flags, (wstring(name) + L"_CSforASLayout").c_str());
	}

	// Compute-shader fallback for mesh shader
	{
		// Convert the descriptor table layouts of AS to CS
		const auto pipelineLayoutMS = convertDescriptorTableLayouts(Shader::Stage::MS, Shader::Stage::CS);
		auto descriptorTableCount = static_cast<uint32_t>(pipelineLayoutMS->GetDescriptorTableLayoutKeys().size());
		pipelineLayoutMS->SetRange(descriptorTableCount++, DescriptorType::UAV, 2, 0, FALLBACK_LAYER_PAYLOAD_SPACE); // VB and IB payloads
		pipelineLayoutMS->SetRootSRV(descriptorTableCount++, 0, FALLBACK_LAYER_PAYLOAD_SPACE, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE); // AS payload buffer
		pipelineLayoutMS->SetConstants(descriptorTableCount, 1, 0, FALLBACK_LAYER_PAYLOAD_SPACE); // Batch index
		pipelineLayout.m_ms = pipelineLayoutMS->GetPipelineLayout(pipelineLayoutCache,
			flags, (wstring(name) + L"_CSforMSLayout").c_str());
	}

	// Vertex-shader fallback for mesh shader
	{
		auto pipelineLayoutVS = convertDescriptorTableLayouts(Shader::Stage::ALL, Shader::Stage::VS);
		auto descriptorTableCount = static_cast<uint32_t>(pipelineLayoutVS->GetDescriptorTableLayoutKeys().size());
		pipelineLayoutVS->SetRange(descriptorTableCount, DescriptorType::SRV, 1, 0);
		pipelineLayoutVS->SetShaderStage(descriptorTableCount++, Shader::Stage::VS);
		pipelineLayoutVS->SetConstants(descriptorTableCount, 1, 0, FALLBACK_LAYER_PAYLOAD_SPACE, Shader::Stage::VS); // Batch index
		pipelineLayout.m_vs = pipelineLayoutVS->GetPipelineLayout(pipelineLayoutCache,
			flags, (wstring(name) + L"_VSforMSLayout").c_str());
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
		state->SetPipelineLayout(pipelineLayout.m_as);
		state->SetShader(csAS);
		pipeline.m_as = state->GetPipeline(computePipelineCache, (wstring(name) + L"_CSforAS").c_str());
	}

	// Compute-shader fallback for mesh shader
	assert(csMS);
	{
		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(pipelineLayout.m_ms);
		state->SetShader(csMS);
		pipeline.m_ms = state->GetPipeline(computePipelineCache, (wstring(name) + L"_CSforMS").c_str());
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

		gState->SetPipelineLayout(pipelineLayout.m_vs);
		gState->SetShader(Shader::Stage::VS, vsMS);
		if (pKey->Shaders[PS]) gState->SetShader(Shader::Stage::PS, reinterpret_cast<Blob::element_type*>(pKey->Shaders[PS]));

		if (pKey->Blend) gState->OMSetBlendState(make_shared<Graphics::Blend::element_type>(*reinterpret_cast<Graphics::Blend::element_type*>(pKey->Blend)));
		if (pKey->Rasterizer) gState->RSSetState(make_shared<Graphics::Rasterizer::element_type>(*reinterpret_cast<Graphics::Rasterizer::element_type*>(pKey->Rasterizer)));
		if (pKey->DepthStencil) gState->DSSetState(make_shared<Graphics::DepthStencil::element_type>(*reinterpret_cast<Graphics::DepthStencil::element_type*>(pKey->DepthStencil)));
		gState->IASetPrimitiveTopologyType(pKey->PrimTopologyType);

		gState->OMSetNumRenderTargets(pKey->NumRenderTargets);
		for (auto i = 0; i < 8; ++i) gState->OMSetRTVFormat(i, pKey->RTVFormats[i]);
		gState->OMSetDSVFormat(pKey->DSVFormat);

		pipeline.m_vs = gState->GetPipeline(graphicsPipelineCache, (wstring(name) + L"_VSforMS").c_str());
	}

	return pipeline;
}

bool MeshShaderFallbackLayer::PipelineLayout::IsValid(bool isMSSupported) const
{
	return (m_native != nullptr) == isMSSupported &&
		m_as && m_ms && m_vs;
}

bool MeshShaderFallbackLayer::Pipeline::IsValid(bool isMSSupported) const
{
	return (m_native != nullptr) == isMSSupported && m_ms && m_vs;
}
