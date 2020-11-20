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
	vector<Resource>& uploaders, uint32_t objCount, const wstring* pFileNames, const ObjectDef* pObjDefs,
	bool isMSSupported)
{
	m_meshShaderFallbackLayer = make_unique<MeshShaderFallbackLayer>(m_device, isMSSupported);
	m_viewport.x = static_cast<float>(width);
	m_viewport.y = static_cast<float>(height);

	// Load inputs
	m_sceneObjects.resize(objCount);
	for (auto i = 0u; i < objCount; ++i)
	{
		Model model;
		model.LoadFromFile(pFileNames[i].c_str());
		const auto meshCount = model.GetMeshCount();

		const auto& def = pObjDefs[i];
		auto& obj = m_sceneObjects[i];
		obj.Meshes.resize(meshCount);

		for (auto j = 0u; j < meshCount; ++j)
		{
			auto& mesh = obj.Meshes[j];
			const auto& meshData = model.GetMesh(j);
			mesh.Subsets.resize(meshData.MeshletSubsets.size());
			memcpy(mesh.Subsets.data(), meshData.MeshletSubsets.data(), sizeof(Subset) * meshData.MeshletSubsets.size());
			mesh.MeshletCount = static_cast<uint32_t>(meshData.Meshlets.size());
			N_RETURN(createMeshBuffers(pCommandList, mesh, meshData, uploaders), false);
		}

		// Convert the transform definition to a matrix
		XMMATRIX world = XMMatrixAffineTransformation(
			XMVectorReplicate(def.Scale),
			g_XMZero,
			XMQuaternionRotationRollPitchYawFromVector(XMLoadFloat3(&def.Rotation)),
			XMLoadFloat3(&def.Position)
		);
		XMStoreFloat3x4(&obj.World, world);

		obj.Instance = ConstantBuffer::MakeUnique();
		N_RETURN(obj.Instance->Create(m_device, sizeof(Instance), FrameCount, nullptr, MemoryType::UPLOAD, L"CBInstance"), false);
		obj.CbvStride = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(obj.Instance->Map(1)) - reinterpret_cast<uint8_t*>(obj.Instance->Map()));
	}

	// Init mesh-shader fallback layer
	{
		auto maxMeshletCount = 0u;
		for (auto& obj : m_sceneObjects)
			for (auto& mesh : obj.Meshes)
				maxMeshletCount = (max)(mesh.MeshletCount, maxMeshletCount);

		struct VertexOut
		{
			float PositionHS[4];
			float PositionVS[3];
			float Normal[3];
			uint32_t MeshletIndex;
		};

		N_RETURN(m_meshShaderFallbackLayer->Init(*m_descriptorTableCache, maxMeshletCount, MAX_VERT_COUNT,
			MAX_PRIM_COUNT, sizeof(VertexOut), AS_GROUP_SIZE), false);
	}

	// Create a depth buffer
	m_depth = DepthStencil::MakeUnique();
	N_RETURN(m_depth->Create(m_device, width, height), false);

	// Create pipelines
	N_RETURN(createPipelineLayouts(isMSSupported), false);
	N_RETURN(createPipelines(rtFormat, m_depth->GetFormat(), isMSSupported), false);
	N_RETURN(createDescriptorTables(), false);

	m_cbGlobals = ConstantBuffer::MakeUnique();
	N_RETURN(m_cbGlobals->Create(m_device, sizeof(Constants), FrameCount, nullptr, MemoryType::UPLOAD, L"CBGbolals"), false);
	m_cbvStride = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(m_cbGlobals->Map(1)) - reinterpret_cast<uint8_t*>(m_cbGlobals->Map()));

	return true;
}

void Renderer::UpdateFrame(uint32_t frameIndex, CXMMATRIX view, const DirectX::XMMATRIX* pProj, const XMFLOAT3& eyePt)
{
	// Global constants
	{
		// Calculate the debug camera's properties to extract plane data.
		const auto aspectRatio = m_viewport.x / m_viewport.y;
		const auto cullFocusDir = XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f);
		const auto cullEyePt = XMVectorSet(0.0f, 10.0f, 21.0f, 1.0f);
		const auto cullView = XMMatrixLookToRH(cullEyePt, cullFocusDir, XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f));
		const auto cullProj = XMMatrixPerspectiveFovRH(XM_PI / 3.0f, aspectRatio, g_zNear, g_zFar);

		// Extract the planes from the debug camera view-projection matrix.
		const auto vp = XMMatrixTranspose(cullView * cullProj);
		const XMVECTOR planes[6] =
		{
			XMPlaneNormalize(vp.r[3] + vp.r[0]), // Left
			XMPlaneNormalize(vp.r[3] - vp.r[0]), // Right
			XMPlaneNormalize(vp.r[3] + vp.r[1]), // Bottom
			XMPlaneNormalize(vp.r[3] - vp.r[1]), // Top
			XMPlaneNormalize(vp.r[2]),           // Near
			XMPlaneNormalize(vp.r[3] - vp.r[2]), // Far
		};

		const auto mainView = pProj ? view : cullView;
		const auto& proj = pProj ? *pProj : cullProj;

		// Set constant data to be read by the shaders.
		const auto pCbData = reinterpret_cast<Constants*>(m_cbGlobals->Map(frameIndex));

		pCbData->ViewPosition = eyePt;
		pCbData->HighlightedIndex = -1;
		pCbData->SelectedIndex = -1;
		pCbData->DrawMeshlets = true;

		XMStoreFloat3x4(&pCbData->View, mainView); // XMStoreFloat3x4 includes transpose.
		XMStoreFloat4x4(&pCbData->ViewProj, XMMatrixTranspose(mainView * proj));
		XMStoreFloat3(&pCbData->CullViewPosition, cullEyePt);

		for (uint32_t i = 0; i < size(planes); ++i)
			XMStoreFloat4(&pCbData->Planes[i], planes[i]);
	}

	// Per instance
	for (auto& obj : m_sceneObjects)
	{
		const auto world = XMLoadFloat3x4(&obj.World);

		XMVECTOR scale, rot, pos;
		XMMatrixDecompose(&scale, &rot, &pos, world);

		const auto pCbData = reinterpret_cast<Instance*>(obj.Instance->Map(frameIndex));
		XMStoreFloat4x4(&pCbData->World, XMMatrixTranspose(world));
		XMStoreFloat3x4(&pCbData->WorldIT, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));
		pCbData->Scale = XMVectorGetX(scale);
		pCbData->Flags = CULL_FLAG | MESHLET_FLAG;
	}
}

void Renderer::Render(Ultimate::CommandList* pCommandList, uint32_t frameIndex,
	const Descriptor& rtv, bool useMeshShader)
{
	const DescriptorPool descriptorPools[] = { m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL) };
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

bool Renderer::createMeshBuffers(CommandList* pCommandList, ObjectMesh& mesh, const Mesh& meshData, std::vector<Resource>& uploaders)
{
	{
		auto& vertices = mesh.Vertices;
		const auto stride = meshData.VertexStrides[0];
		const auto numElements = static_cast<uint32_t>(meshData.Vertices[0].size()) / stride;
		vertices = StructuredBuffer::MakeUnique();
		N_RETURN(vertices->Create(m_device, numElements, stride, ResourceFlag::NONE, MemoryType::DEFAULT), false);
		uploaders.push_back(nullptr);

		N_RETURN(vertices->Upload(pCommandList, uploaders.back(), meshData.Vertices[0].data(), stride * numElements, 0,
			ResourceState::NON_PIXEL_SHADER_RESOURCE), false);
	}

	{
		auto& meshlets = mesh.Meshlets;
		const uint32_t stride = sizeof(Meshlet);
		const auto numElements = static_cast<uint32_t>(meshData.Meshlets.size());
		meshlets = StructuredBuffer::MakeUnique();
		N_RETURN(meshlets->Create(m_device, numElements, stride, ResourceFlag::NONE, MemoryType::DEFAULT), false);
		uploaders.push_back(nullptr);

		N_RETURN(meshlets->Upload(pCommandList, uploaders.back(), meshData.Meshlets.data(), stride * numElements, 0,
			ResourceState::NON_PIXEL_SHADER_RESOURCE), false);
	}

	{
		auto& primitiveIndices = mesh.PrimitiveIndices;
		const uint32_t stride = sizeof(PackedTriangle);
		const auto numElements = static_cast<uint32_t>(meshData.PrimitiveIndices.size());
		primitiveIndices = StructuredBuffer::MakeUnique();
		N_RETURN(primitiveIndices->Create(m_device, numElements, stride, ResourceFlag::NONE, MemoryType::DEFAULT), false);
		uploaders.push_back(nullptr);

		N_RETURN(primitiveIndices->Upload(pCommandList, uploaders.back(), meshData.PrimitiveIndices.data(), stride * numElements, 0,
			ResourceState::NON_PIXEL_SHADER_RESOURCE), false);
	}

	{
		auto& uniqueVertexIndices = mesh.UniqueVertexIndices;
		uniqueVertexIndices = RawBuffer::MakeUnique();
		const auto byteWidth = meshData.UniqueVertexIndices.size();
		N_RETURN(uniqueVertexIndices->Create(m_device, byteWidth, ResourceFlag::NONE, MemoryType::DEFAULT), false);
		uploaders.push_back(nullptr);

		N_RETURN(uniqueVertexIndices->Upload(pCommandList, uploaders.back(), meshData.UniqueVertexIndices.data(), byteWidth, 0,
			ResourceState::NON_PIXEL_SHADER_RESOURCE), false);
	}

	{
		auto& meshletCullData = mesh.MeshletCullData;
		const uint32_t stride = sizeof(CullData);
		const auto numElements = static_cast<uint32_t>(meshData.CullingData.size());
		meshletCullData = StructuredBuffer::MakeUnique();
		N_RETURN(meshletCullData->Create(m_device, numElements, stride, ResourceFlag::NONE, MemoryType::DEFAULT), false);
		uploaders.push_back(nullptr);

		N_RETURN(meshletCullData->Upload(pCommandList, uploaders.back(), meshData.CullingData.data(), stride * numElements, 0,
			ResourceState::NON_PIXEL_SHADER_RESOURCE), false);
	}

	{
		auto& meshInfo = mesh.MeshInfo;
		meshInfo = ConstantBuffer::MakeUnique();
		N_RETURN(meshInfo->Create(m_device, sizeof(MeshInfo), 1, nullptr, MemoryType::DEFAULT, L"CBMeshInfo"), false);
		uploaders.push_back(nullptr);

		MeshInfo info = {};
		info.IndexSize = meshData.IndexSize;
		info.MeshletCount = static_cast<uint32_t>(meshData.Meshlets.size());
		info.LastMeshletVertCount = meshData.Meshlets.back().VertCount;
		info.LastMeshletPrimCount = meshData.Meshlets.back().PrimCount;

		N_RETURN(meshInfo->Upload(pCommandList, uploaders.back(), &info, sizeof(info)), false);
	}

	return true;
}

bool Renderer::createPipelineLayouts(bool isMSSupported)
{
	// Meshlet-culling pipeline layout
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRootCBV(CBV_GLOBALS, 0, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetRootCBV(CBV_MESHINFO, 1, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetRootCBV(CBV_INSTANCE, 2, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetRange(SRV_INPUTS, DescriptorType::SRV, 4, 0, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetShaderStage(SRV_INPUTS, Shader::MS);
		pipelineLayout->SetRootSRV(SRV_CULL, 4, 0, DescriptorFlag::DATA_STATIC, Shader::AS);
		m_pipelineLayout = m_meshShaderFallbackLayer->GetPipelineLayout(pipelineLayout, *m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"MeshletLayout");

		N_RETURN(m_pipelineLayout.IsValid(isMSSupported), false);
	}

	return true;
}

bool Renderer::createPipelines(Format rtFormat, Format dsFormat, bool isMSSupported)
{
	// Meshlet-culling pipeline
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::AS, AS_MESHLET, L"ASMeshlet.cso"), false);
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::MS, MS_MESHLET, L"MSMeshlet.cso"), false);
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, CS_MESHLET_AS, L"CSMeshletAS.cso"), false);
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, CS_MESHLET_MS, L"CSMeshletMS.cso"), false);
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::VS, VS_MESHLET, L"VSMeshlet.cso"), false);
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::PS, PS_MESHLET, L"PSMeshlet.cso"), false);

		const auto state = MeshShader::State::MakeUnique();
		state->SetShader(Shader::Stage::AS, m_shaderPool->GetShader(Shader::Stage::AS, AS_MESHLET));
		state->SetShader(Shader::Stage::MS, m_shaderPool->GetShader(Shader::Stage::MS, MS_MESHLET));
		state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, PS_MESHLET));
		state->OMSetNumRenderTargets(1);
		state->OMSetRTVFormat(0, rtFormat);
		state->OMSetDSVFormat(dsFormat);
		m_pipeline = m_meshShaderFallbackLayer->GetPipeline(m_pipelineLayout, m_shaderPool->GetShader(Shader::Stage::CS, CS_MESHLET_AS),
			m_shaderPool->GetShader(Shader::Stage::CS, CS_MESHLET_MS), m_shaderPool->GetShader(Shader::Stage::VS, VS_MESHLET),
			state, *m_meshShaderPipelineCache, *m_computePipelineCache, *m_graphicsPipelineCache, L"MeshletPipe");

		N_RETURN(m_pipeline.IsValid(isMSSupported), false);
	}

	return true;
}

bool Renderer::createDescriptorTables()
{
	// Meshlet SRVs
	for (auto& obj : m_sceneObjects)
	{
		for (auto& mesh : obj.Meshes)
		{
			const Descriptor descriptors[] =
			{
				mesh.Vertices->GetSRV(),
				mesh.Meshlets->GetSRV(),
				mesh.UniqueVertexIndices->GetSRV(),
				mesh.PrimitiveIndices->GetSRV()
			};
			const auto descriptorTable = Util::DescriptorTable::MakeUnique();
			descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
			X_RETURN(mesh.SrvTable, descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
		}
	}

	return true;
}

void Renderer::renderMS(Ultimate::CommandList* pCommandList, uint32_t frameIndex)
{
	// Set descriptor tables
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayout.m_native);
	pCommandList->SetGraphicsRootConstantBufferView(CBV_GLOBALS, m_cbGlobals->GetResource(), m_cbvStride * frameIndex);

	// Set pipeline state
	pCommandList->SetPipelineState(m_pipeline.m_native);

	// Record commands.
	for (auto& obj : m_sceneObjects)
	{
		pCommandList->SetGraphicsRootConstantBufferView(CBV_INSTANCE, obj.Instance->GetResource(), obj.CbvStride * frameIndex);

		for (auto& mesh : obj.Meshes)
		{
			pCommandList->SetGraphicsRootConstantBufferView(CBV_MESHINFO, mesh.MeshInfo->GetResource());
			pCommandList->SetGraphicsDescriptorTable(SRV_INPUTS, mesh.SrvTable);
			pCommandList->SetGraphicsRootShaderResourceView(SRV_CULL, mesh.MeshletCullData->GetResource());
			pCommandList->DispatchMesh(DIV_UP(mesh.MeshletCount, AS_GROUP_SIZE), 1, 1);
		}
	}
}

void Renderer::renderFallback(XUSG::Ultimate::CommandList* pCommandList, uint32_t frameIndex)
{
	m_meshShaderFallbackLayer->EnableNativeMeshShader(false);

	// Set descriptor tables
	m_meshShaderFallbackLayer->SetPipelineLayout(pCommandList, m_pipelineLayout);
	m_meshShaderFallbackLayer->SetRootConstantBufferView(pCommandList, CBV_GLOBALS, m_cbGlobals->GetResource(), m_cbvStride * frameIndex);

	// Set pipeline state
	m_meshShaderFallbackLayer->SetPipelineState(pCommandList, m_pipeline);

	// Record commands.
	for (auto& obj : m_sceneObjects)
	{
		m_meshShaderFallbackLayer->SetRootConstantBufferView(pCommandList, CBV_INSTANCE, obj.Instance->GetResource(), obj.CbvStride * frameIndex);

		for (auto& mesh : obj.Meshes)
		{
			m_meshShaderFallbackLayer->SetRootConstantBufferView(pCommandList, CBV_MESHINFO, mesh.MeshInfo->GetResource());
			m_meshShaderFallbackLayer->SetDescriptorTable(pCommandList, SRV_INPUTS, mesh.SrvTable);
			m_meshShaderFallbackLayer->SetRootShaderResourceView(pCommandList, SRV_CULL, mesh.MeshletCullData->GetResource());
			m_meshShaderFallbackLayer->DispatchMesh(pCommandList, DIV_UP(mesh.MeshletCount, AS_GROUP_SIZE), 1, 1);
		}
	}
}
