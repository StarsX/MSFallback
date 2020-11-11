//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedConst.h"
#include "Renderer.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

Renderer::Renderer(const Device& device) :
	m_device(device)
{
	m_shaderPool = ShaderPool::MakeUnique();
	m_graphicsPipelineCache = Graphics::PipelineCache::MakeUnique(device);
	m_computePipelineCache = Compute::PipelineCache::MakeUnique(device);
	m_meshShaderPipelineCache = MeshShader::PipelineCache::MakeUnique(device);
	m_pipelineLayoutCache = PipelineLayoutCache::MakeUnique(device);
	m_descriptorTableCache = DescriptorTableCache::MakeUnique(device, L"DescriptorTableCache");
}

Renderer::~Renderer()
{
}

bool Renderer::Init(CommandList* pCommandList, uint32_t width, uint32_t height, Format rtFormat,
	vector<Resource>& uploaders, const wchar_t* fileName, const XMFLOAT4& posScale, bool isMSSupported)
{
	m_viewport.x = static_cast<float>(width);
	m_viewport.y = static_cast<float>(height);
	m_posScale = posScale;

	// Load inputs
	Model model;
	model.LoadFromFile(fileName);
	const auto meshCount = model.GetMeshCount();
	m_subsets.resize(meshCount);
	m_indexBytes.resize(meshCount);
	m_vertices.resize(meshCount);
	m_meshlets.resize(meshCount);
	m_primitiveIndices.resize(meshCount);
	m_uniqueVertexIndices.resize(meshCount);
	for (auto i = 0u; i < meshCount; ++i)
	{
		const auto& mesh = model.GetMesh(i);
		m_subsets[i].resize(mesh.MeshletSubsets.size());
		memcpy(m_subsets[i].data(), mesh.MeshletSubsets.data(), sizeof(Subset) * mesh.MeshletSubsets.size());
		m_indexBytes[i] = mesh.IndexSize;
		N_RETURN(createMeshBuffers(pCommandList, i, mesh, uploaders), false);
	}

	N_RETURN(createPayloadBuffers(), false);

	// Create a depth buffer
	m_depth = DepthStencil::MakeUnique();
	N_RETURN(m_depth->Create(m_device, width, height), false);

	// Create pipelines
	N_RETURN(createPipelineLayouts(isMSSupported), false);
	N_RETURN(createPipelines(rtFormat, m_depth->GetFormat(), isMSSupported), false);
	N_RETURN(createDescriptorTables(), false);

	m_cbGlobals = ConstantBuffer::MakeUnique();
	N_RETURN(m_cbGlobals->Create(m_device, sizeof(CBGlobals), FrameCount, nullptr, MemoryType::UPLOAD, L"CBGbolals"), false);
	m_cbvStride = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(m_cbGlobals->Map(1)) - reinterpret_cast<uint8_t*>(m_cbGlobals->Map()));

	return true;
}

void Renderer::UpdateFrame(uint32_t frameIndex, CXMMATRIX view, CXMMATRIX proj)
{
	// General matrices
	const auto world = XMMatrixScaling(m_posScale.w, m_posScale.w, m_posScale.w) *
		XMMatrixTranslation(m_posScale.x, m_posScale.y, m_posScale.z);
	const auto worldView = world * view;
	const auto worldViewProj = worldView * proj;
	
	// Global constants
	{
		const auto pCbData = reinterpret_cast<CBGlobals*>(m_cbGlobals->Map(frameIndex));
		XMStoreFloat4x4(&pCbData->World, XMMatrixTranspose(world));
		XMStoreFloat4x4(&pCbData->WorldView, XMMatrixTranspose(worldView));
		XMStoreFloat4x4(&pCbData->WorldViewProj, XMMatrixTranspose(worldViewProj));
		pCbData->DrawMeshlets = true;
	}
}

void Renderer::Render(Ultimate::CommandList* pCommandList, uint32_t frameIndex,
	const Descriptor& rtv, bool useMeshShader)
{
	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
		m_descriptorTableCache->GetDescriptorPool(SAMPLER_POOL)
	};
	pCommandList->SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	// Clear depth
	pCommandList->ClearDepthStencilView(m_depth->GetDSV(), ClearFlag::DEPTH, 1.0f);

	// Set viewport
	Viewport viewport(0.0f, 0.0f, m_viewport.x, m_viewport.y);
	RectRange scissorRect(0, 0, static_cast<long>(m_viewport.x), static_cast<long>(m_viewport.y));
	pCommandList->RSSetViewports(1, &viewport);
	pCommandList->RSSetScissorRects(1, &scissorRect);

	pCommandList->OMSetRenderTargets(1, &rtv, &m_depth->GetDSV());

	if (useMeshShader) renderMS(pCommandList, frameIndex);
	else renderFallback(pCommandList, frameIndex);
}

bool Renderer::createMeshBuffers(CommandList* pCommandList, uint32_t i, const Mesh& mesh, std::vector<Resource>& uploaders)
{
	{
		auto& vertices = m_vertices[i];
		const auto stride = mesh.VertexStrides[0];
		const auto numElements = static_cast<uint32_t>(mesh.Vertices[0].size()) / stride;
		vertices = StructuredBuffer::MakeUnique();
		N_RETURN(vertices->Create(m_device, numElements, stride, ResourceFlag::NONE, MemoryType::DEFAULT), false);
		uploaders.push_back(nullptr);

		N_RETURN(vertices->Upload(pCommandList, uploaders.back(), mesh.Vertices[0].data(), stride * numElements, 0,
			ResourceState::NON_PIXEL_SHADER_RESOURCE), false);
	}

	{
		auto& meshlets = m_meshlets[i];
		const uint32_t stride = sizeof(Meshlet);
		const auto numElements = static_cast<uint32_t>(mesh.Meshlets.size());
		meshlets = StructuredBuffer::MakeUnique();
		N_RETURN(meshlets->Create(m_device, numElements, stride, ResourceFlag::NONE, MemoryType::DEFAULT), false);
		uploaders.push_back(nullptr);

		N_RETURN(meshlets->Upload(pCommandList, uploaders.back(), mesh.Meshlets.data(), stride * numElements, 0,
			ResourceState::NON_PIXEL_SHADER_RESOURCE), false);
	}

	{
		auto& primitiveIndices = m_primitiveIndices[i];
		const uint32_t stride = sizeof(PackedTriangle);
		const auto numElements = static_cast<uint32_t>(mesh.PrimitiveIndices.size());
		primitiveIndices = StructuredBuffer::MakeUnique();
		N_RETURN(primitiveIndices->Create(m_device, numElements, stride, ResourceFlag::NONE, MemoryType::DEFAULT), false);
		uploaders.push_back(nullptr);

		N_RETURN(primitiveIndices->Upload(pCommandList, uploaders.back(), mesh.PrimitiveIndices.data(), stride * numElements, 0,
			ResourceState::NON_PIXEL_SHADER_RESOURCE), false);
	}

	{
		auto& uniqueVertexIndices = m_uniqueVertexIndices[i];
		uniqueVertexIndices = RawBuffer::MakeUnique();
		const auto byteWidth = mesh.UniqueVertexIndices.size();
		N_RETURN(uniqueVertexIndices->Create(m_device, byteWidth, ResourceFlag::NONE, MemoryType::DEFAULT), false);
		uploaders.push_back(nullptr);

		N_RETURN(uniqueVertexIndices->Upload(pCommandList, uploaders.back(), mesh.UniqueVertexIndices.data(), byteWidth, 0,
			ResourceState::NON_PIXEL_SHADER_RESOURCE), false);
	}

	return true;
}

bool Renderer::createPayloadBuffers()
{
	auto maxMeshletCount = 0u;
	for (auto& subsets : m_subsets)
		for (const auto& subset : subsets)
			maxMeshletCount = (max)(subset.Count, maxMeshletCount);

	struct VertexOut
	{
		float PositionHS[4];
		float PositionVS[3];
		float Normal[3];
		uint32_t MeshletIndex;
	};

	m_vertPayloads = StructuredBuffer::MakeUnique();
	N_RETURN(m_vertPayloads->Create(m_device, 64 * maxMeshletCount, sizeof(VertexOut), ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT), false);

	m_primIdxPayloads = StructuredBuffer::MakeUnique();
	N_RETURN(m_primIdxPayloads->Create(m_device, 126 * maxMeshletCount, sizeof(uint32_t), ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT), false);

	return true;
}

bool Renderer::createPipelineLayouts(bool isMSSupported)
{
	// Mesh-shader
	if (isMSSupported)
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRootCBV(CBV_GLOBALS, 0, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetConstants(CONSTANTS, SizeOfInUint32(uint32_t[2]), 1, 0, Shader::MS);
		pipelineLayout->SetRange(SRVS, DescriptorType::SRV, 4, 0, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetShaderStage(SRVS, Shader::MS);
		X_RETURN(m_pipelineLayouts[MESHLET_MS_LAYOUT], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"MSMeshletLayout"), false);
	}

	// Fallback compute-shader
	{
		// Get pipeline layout
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRootCBV(CBV_GLOBALS, 0, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetConstants(CONSTANTS, SizeOfInUint32(uint32_t[2]), 1);
		pipelineLayout->SetRange(SRVS, DescriptorType::SRV, 4, 0);
		pipelineLayout->SetRange(UAVS, DescriptorType::UAV, 2, 0);
		X_RETURN(m_pipelineLayouts[MESHLET_CS_LAYOUT], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"CSMeshletLayout"), false);
	}

	// Fallback vertex-shader
	{
		// Get pipeline layout
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRootCBV(CBV_GLOBALS, 0, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 2, 0);
		pipelineLayout->SetShaderStage(1, Shader::VS);
		X_RETURN(m_pipelineLayouts[MESHLET_VS_LAYOUT], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"VSMeshletLayout"), false);
	}

	return true;
}

bool Renderer::createPipelines(Format rtFormat, Format dsFormat, bool isMSSupported)
{
	N_RETURN(m_shaderPool->CreateShader(Shader::Stage::PS, PS_MESHLET, L"PSMeshlet.cso"), false);

	// Mesh-shader
	if (isMSSupported)
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::MS, MS_MESHLET, L"MSMeshlet.cso"), false);

		const auto state = MeshShader::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[MESHLET_MS_LAYOUT]);
		state->SetShader(Shader::Stage::MS, m_shaderPool->GetShader(Shader::Stage::MS, MS_MESHLET));
		state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, PS_MESHLET));
		state->OMSetNumRenderTargets(1);
		state->OMSetRTVFormat(0, rtFormat);
		state->OMSetDSVFormat(dsFormat);
		X_RETURN(m_pipelines[MESHLET_MS], state->GetPipeline(*m_meshShaderPipelineCache, L"MeshShaderMeshlet"), false);
	}

	// Fallback compute-shader
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, CS_MESHLET, L"CSMeshlet.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[MESHLET_CS_LAYOUT]);
		state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, CS_MESHLET));
		X_RETURN(m_pipelines[MESHLET_CS], state->GetPipeline(*m_computePipelineCache, L"ComputeShaderMeshlet"), false);
	}

	// Fallback vertex-shader
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::VS, VS_MESHLET, L"VSMeshlet.cso"), false);
		
		const auto state = Graphics::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[MESHLET_VS_LAYOUT]);
		state->SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, VS_MESHLET));
		state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, PS_MESHLET));
		state->OMSetNumRenderTargets(1);
		state->OMSetRTVFormat(0, rtFormat);
		state->OMSetDSVFormat(dsFormat);
		X_RETURN(m_pipelines[MESHLET_VS], state->GetPipeline(*m_graphicsPipelineCache, L"VertexShaderMeshlet"), false);
	}

	return true;
}

bool Renderer::createDescriptorTables()
{
	const auto meshCount = static_cast<uint32_t>(m_subsets.size());

	// SRVs
	m_srvTables.resize(meshCount);
	for (auto i = 0u; i < meshCount; ++i)
	{
		const Descriptor descriptors[] =
		{
			m_vertices[i]->GetSRV(),
			m_meshlets[i]->GetSRV(),
			m_uniqueVertexIndices[i]->GetSRV(),
			m_primitiveIndices[i]->GetSRV()
		};
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_srvTables[i], descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	// Payload UAVs
	{
		const Descriptor descriptors[] =
		{
			m_vertPayloads->GetUAV(),
			m_primIdxPayloads->GetUAV()
		};
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_uavTable, descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	// Payload SRVs
	{
		const Descriptor descriptors[] =
		{
			m_vertPayloads->GetSRV(),
			m_primIdxPayloads->GetSRV()
		};
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_srvTable, descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	// Create the sampler table
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const auto sampler = LINEAR_CLAMP;
		descriptorTable->SetSamplers(0, 1, &sampler, *m_descriptorTableCache);
		X_RETURN(m_samplerTable, descriptorTable->GetSamplerTable(*m_descriptorTableCache), false);
	}

	return true;
}

void Renderer::renderMS(Ultimate::CommandList* pCommandList, uint32_t frameIndex)
{
	// Set descriptor tables
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[MESHLET_MS_LAYOUT]);
	pCommandList->SetGraphicsRootConstantBufferView(CBV_GLOBALS, m_cbGlobals->GetResource(), m_cbvStride * frameIndex);

	// Set pipeline state
	pCommandList->SetPipelineState(m_pipelines[MESHLET_MS]);

	// Record commands.
	const auto meshCount = static_cast<uint32_t>(m_subsets.size());
	for (auto i = 0u; i < meshCount; ++i)
	{
		pCommandList->SetGraphics32BitConstant(CONSTANTS, m_indexBytes[i]);
		pCommandList->SetGraphicsDescriptorTable(SRVS, m_srvTables[i]);

		for (const auto& subset : m_subsets[i])
		{
			pCommandList->SetGraphics32BitConstant(CONSTANTS, subset.Offset, 1);
			pCommandList->DispatchMesh(subset.Count, 1, 1);
		}
	}
}

void Renderer::renderFallback(CommandList* pCommandList, uint32_t frameIndex)
{
	const auto meshCount = static_cast<uint32_t>(m_subsets.size());
	for (auto i = 0u; i < meshCount; ++i)
	{
		for (const auto& subset : m_subsets[i])
		{
			// Set barriers
			ResourceBarrier barriers[2];
			auto numBarriers = m_vertPayloads->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
			numBarriers = m_primIdxPayloads->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);

			// Set descriptor tables
			pCommandList->SetComputePipelineLayout(m_pipelineLayouts[MESHLET_CS_LAYOUT]);
			pCommandList->SetComputeRootConstantBufferView(CBV_GLOBALS, m_cbGlobals->GetResource(), m_cbvStride * frameIndex);
			pCommandList->SetCompute32BitConstant(CONSTANTS, m_indexBytes[i]);
			pCommandList->SetComputeDescriptorTable(SRVS, m_srvTables[i]);
			pCommandList->SetComputeDescriptorTable(UAVS, m_uavTable);

			// Set pipeline state
			pCommandList->SetPipelineState(m_pipelines[MESHLET_CS]);

			// Record commands.
			pCommandList->SetCompute32BitConstant(CONSTANTS, subset.Offset, 1);
			pCommandList->Dispatch(subset.Count, 1, 1);

			// Set barriers
			numBarriers = m_vertPayloads->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE);
			numBarriers = m_primIdxPayloads->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
			pCommandList->Barrier(numBarriers, barriers);

			// Set descriptor tables
			pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[MESHLET_VS_LAYOUT]);
			pCommandList->SetGraphicsRootConstantBufferView(CBV_GLOBALS, m_cbGlobals->GetResource(), m_cbvStride * frameIndex);
			pCommandList->SetGraphicsDescriptorTable(1, m_srvTable);

			// Set pipeline state
			pCommandList->SetPipelineState(m_pipelines[MESHLET_VS]);

			// Record commands.
			pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);
			pCommandList->Draw(3 * 126, subset.Count, 0, 0);
		}
	}
}
