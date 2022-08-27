//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedConst.h"
#include "Renderer.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

Renderer::Renderer()
{
	m_shaderPool = ShaderPool::MakeUnique();
}

Renderer::~Renderer()
{
}

bool Renderer::Init(CommandList* pCommandList, const DescriptorTableCache::sptr& descriptorTableCache,
	uint32_t width, uint32_t height, Format rtFormat, vector<Resource::uptr>& uploaders, uint32_t objCount,
	const wstring* pFileNames, const ObjectDef* pObjDefs, bool isMSSupported)
{
	const auto pDevice = pCommandList->GetDevice();
	m_graphicsPipelineCache = Graphics::PipelineCache::MakeUnique(pDevice);
	m_computePipelineCache = Compute::PipelineCache::MakeUnique(pDevice);
	m_meshShaderPipelineCache = MeshShader::PipelineCache::MakeUnique(pDevice);
	m_pipelineLayoutCache = PipelineLayoutCache::MakeUnique(pDevice);
	m_descriptorTableCache = descriptorTableCache;
	m_meshShaderFallbackLayer = make_unique<MeshShaderFallbackLayer>(isMSSupported);

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
			XUSG_N_RETURN(createMeshBuffers(pCommandList, mesh, meshData, uploaders), false);
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
		XUSG_N_RETURN(obj.Instance->Create(pDevice, sizeof(Instance[FrameCount]), FrameCount,
			nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"CBInstance"), false);
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

		XUSG_N_RETURN(m_meshShaderFallbackLayer->Init(pDevice, m_descriptorTableCache.get(), maxMeshletCount,
			MAX_VERTS, MAX_PRIMS, sizeof(VertexOut), AS_GROUP_SIZE), false);
	}

	// Create a depth buffer
	m_depth = DepthStencil::MakeUnique();
	XUSG_N_RETURN(m_depth->Create(pDevice, width, height, Format::D24_UNORM_S8_UINT, ResourceFlag::DENY_SHADER_RESOURCE), false);

	// Create pipelines
	XUSG_N_RETURN(createPipelineLayouts(isMSSupported), false);
	XUSG_N_RETURN(createPipelines(rtFormat, m_depth->GetFormat(), isMSSupported), false);
	XUSG_N_RETURN(createDescriptorTables(), false);

	m_cbGlobals = ConstantBuffer::MakeUnique();
	XUSG_N_RETURN(m_cbGlobals->Create(pDevice, sizeof(Constants[FrameCount]), FrameCount,
		nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"CBGbolals"), false);

	return true;
}

void Renderer::UpdateFrame(uint8_t frameIndex, CXMMATRIX view, const DirectX::XMMATRIX* pProj, const XMFLOAT3& eyePt)
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

void Renderer::Render(Ultimate::CommandList* pCommandList, uint8_t frameIndex,
	const Descriptor& rtv, bool useMeshShader)
{
	// Set descriptor tables
	m_meshShaderFallbackLayer->EnableNativeMeshShader(useMeshShader);
	m_meshShaderFallbackLayer->SetPipelineLayout(pCommandList, m_pipelineLayout);
	m_meshShaderFallbackLayer->SetRootConstantBufferView(pCommandList, CBV_GLOBALS, m_cbGlobals.get(), m_cbGlobals->GetCBVOffset(frameIndex));

	// Set pipeline state
	m_meshShaderFallbackLayer->SetPipelineState(pCommandList, m_pipeline);

	// Clear depth
	pCommandList->OMSetRenderTargets(1, &rtv, &m_depth->GetDSV());
	pCommandList->ClearDepthStencilView(m_depth->GetDSV(), ClearFlag::DEPTH, 1.0f);

	// Set viewport
	Viewport viewport(0.0f, 0.0f, m_viewport.x, m_viewport.y);
	RectRange scissorRect(0, 0, static_cast<long>(m_viewport.x), static_cast<long>(m_viewport.y));
	pCommandList->RSSetViewports(1, &viewport);
	pCommandList->RSSetScissorRects(1, &scissorRect);

	// Record commands.
	for (auto& obj : m_sceneObjects)
	{
		m_meshShaderFallbackLayer->SetRootConstantBufferView(pCommandList, CBV_INSTANCE, obj.Instance.get(), obj.Instance->GetCBVOffset(frameIndex));

		for (auto& mesh : obj.Meshes)
		{
			m_meshShaderFallbackLayer->SetRootConstantBufferView(pCommandList, CBV_MESHINFO, mesh.MeshInfo.get());
			m_meshShaderFallbackLayer->SetDescriptorTable(pCommandList, SRV_INPUTS, mesh.SrvTable);
			m_meshShaderFallbackLayer->SetRootShaderResourceView(pCommandList, SRV_CULL, mesh.MeshletCullData.get());
			m_meshShaderFallbackLayer->DispatchMesh(pCommandList, XUSG_DIV_UP(mesh.MeshletCount, AS_GROUP_SIZE), 1, 1);
		}
	}
}

bool Renderer::createMeshBuffers(CommandList* pCommandList, ObjectMesh& mesh,
	const Mesh& meshData, vector<Resource::uptr>& uploaders)
{
	const auto pDevice = pCommandList->GetDevice();

	{
		auto& vertices = mesh.Vertices;
		const auto stride = meshData.VertexStrides[0];
		const auto numElements = static_cast<uint32_t>(meshData.Vertices[0].size()) / stride;
		vertices = StructuredBuffer::MakeUnique();
		XUSG_N_RETURN(vertices->Create(pDevice, numElements, stride, ResourceFlag::NONE, MemoryType::DEFAULT), false);
		uploaders.emplace_back(Resource::MakeUnique());

		XUSG_N_RETURN(vertices->Upload(pCommandList, uploaders.back().get(), meshData.Vertices[0].data(),
			stride * numElements, 0, ResourceState::NON_PIXEL_SHADER_RESOURCE), false);
	}

	{
		auto& meshlets = mesh.Meshlets;
		const uint32_t stride = sizeof(Meshlet);
		const auto numElements = static_cast<uint32_t>(meshData.Meshlets.size());
		meshlets = StructuredBuffer::MakeUnique();
		XUSG_N_RETURN(meshlets->Create(pDevice, numElements, stride, ResourceFlag::NONE, MemoryType::DEFAULT), false);
		uploaders.emplace_back(Resource::MakeUnique());

		XUSG_N_RETURN(meshlets->Upload(pCommandList, uploaders.back().get(), meshData.Meshlets.data(),
			stride * numElements, 0, ResourceState::NON_PIXEL_SHADER_RESOURCE), false);
	}

	{
		auto& primitiveIndices = mesh.PrimitiveIndices;
		const uint32_t stride = sizeof(PackedTriangle);
		const auto numElements = static_cast<uint32_t>(meshData.PrimitiveIndices.size());
		primitiveIndices = StructuredBuffer::MakeUnique();
		XUSG_N_RETURN(primitiveIndices->Create(pDevice, numElements, stride, ResourceFlag::NONE, MemoryType::DEFAULT), false);
		uploaders.emplace_back(Resource::MakeUnique());

		XUSG_N_RETURN(primitiveIndices->Upload(pCommandList, uploaders.back().get(), meshData.PrimitiveIndices.data(),
			stride * numElements, 0, ResourceState::NON_PIXEL_SHADER_RESOURCE), false);
	}

	{
		auto& uniqueVertexIndices = mesh.UniqueVertexIndices;
		uniqueVertexIndices = RawBuffer::MakeUnique();
		const auto byteWidth = meshData.UniqueVertexIndices.size();
		XUSG_N_RETURN(uniqueVertexIndices->Create(pDevice, byteWidth, ResourceFlag::NONE, MemoryType::DEFAULT), false);
		uploaders.emplace_back(Resource::MakeUnique());

		XUSG_N_RETURN(uniqueVertexIndices->Upload(pCommandList, uploaders.back().get(), meshData.UniqueVertexIndices.data(),
			byteWidth, 0, ResourceState::NON_PIXEL_SHADER_RESOURCE), false);
	}

	{
		auto& meshletCullData = mesh.MeshletCullData;
		const uint32_t stride = sizeof(CullData);
		const auto numElements = static_cast<uint32_t>(meshData.CullingData.size());
		meshletCullData = StructuredBuffer::MakeUnique();
		XUSG_N_RETURN(meshletCullData->Create(pDevice, numElements, stride, ResourceFlag::NONE, MemoryType::DEFAULT), false);
		uploaders.emplace_back(Resource::MakeUnique());

		XUSG_N_RETURN(meshletCullData->Upload(pCommandList, uploaders.back().get(), meshData.CullingData.data(),
			stride * numElements, 0, ResourceState::NON_PIXEL_SHADER_RESOURCE), false);
	}

	{
		auto& meshInfo = mesh.MeshInfo;
		meshInfo = ConstantBuffer::MakeUnique();
		XUSG_N_RETURN(meshInfo->Create(pDevice, sizeof(MeshInfo), 1, nullptr,
			MemoryType::DEFAULT, MemoryFlag::NONE, L"CBMeshInfo"), false);
		uploaders.emplace_back(Resource::MakeUnique());

		MeshInfo info = {};
		info.IndexSize = meshData.IndexSize;
		info.MeshletCount = static_cast<uint32_t>(meshData.Meshlets.size());
		info.LastMeshletVertCount = meshData.Meshlets.back().VertCount;
		info.LastMeshletPrimCount = meshData.Meshlets.back().PrimCount;

		XUSG_N_RETURN(meshInfo->Upload(pCommandList, uploaders.back().get(), &info, sizeof(info)), false);
	}

	return true;
}

bool Renderer::createPipelineLayouts(bool isMSSupported)
{
	// Meshlet-culling pipeline layout
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRootCBV(CBV_GLOBALS, 0);
		pipelineLayout->SetRootCBV(CBV_MESHINFO, 1);
		pipelineLayout->SetRootCBV(CBV_INSTANCE, 2);
		pipelineLayout->SetRange(SRV_INPUTS, DescriptorType::SRV, 4, 0, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetShaderStage(SRV_INPUTS, Shader::MS);
		pipelineLayout->SetRootSRV(SRV_CULL, 4, 0, DescriptorFlag::DATA_STATIC, Shader::AS);
		m_pipelineLayout = m_meshShaderFallbackLayer->GetPipelineLayout(pipelineLayout.get(), m_pipelineLayoutCache.get(),
			PipelineLayoutFlag::NONE, L"MeshletLayout");

		XUSG_N_RETURN(m_pipelineLayout.IsValid(isMSSupported), false);
	}

	return true;
}

bool Renderer::createPipelines(Format rtFormat, Format dsFormat, bool isMSSupported)
{
	// Meshlet-culling pipeline
	{
		XUSG_N_RETURN(m_shaderPool->CreateShader(Shader::Stage::AS, AS_MESHLET, L"ASMeshlet.cso"), false);
		XUSG_N_RETURN(m_shaderPool->CreateShader(Shader::Stage::MS, MS_MESHLET, L"MSMeshlet.cso"), false);
		XUSG_N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, CS_MESHLET_AS, L"CSMeshletAS.cso"), false);
		XUSG_N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, CS_MESHLET_MS, L"CSMeshletMS.cso"), false);
		XUSG_N_RETURN(m_shaderPool->CreateShader(Shader::Stage::VS, VS_MESHLET, L"VSMeshlet.cso"), false);
		XUSG_N_RETURN(m_shaderPool->CreateShader(Shader::Stage::PS, PS_MESHLET, L"PSMeshlet.cso"), false);

		const auto state = MeshShader::State::MakeUnique();
		state->SetShader(Shader::Stage::AS, m_shaderPool->GetShader(Shader::Stage::AS, AS_MESHLET));
		state->SetShader(Shader::Stage::MS, m_shaderPool->GetShader(Shader::Stage::MS, MS_MESHLET));
		state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, PS_MESHLET));
		state->OMSetNumRenderTargets(1);
		state->OMSetRTVFormat(0, rtFormat);
		state->OMSetDSVFormat(dsFormat);
		m_pipeline = m_meshShaderFallbackLayer->GetPipeline(m_pipelineLayout, m_shaderPool->GetShader(Shader::Stage::CS, CS_MESHLET_AS),
			m_shaderPool->GetShader(Shader::Stage::CS, CS_MESHLET_MS), m_shaderPool->GetShader(Shader::Stage::VS, VS_MESHLET),
			state.get(), m_meshShaderPipelineCache.get(), m_computePipelineCache.get(), m_graphicsPipelineCache.get(), L"MeshletPipe");

		XUSG_N_RETURN(m_pipeline.IsValid(isMSSupported), false);
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
			XUSG_X_RETURN(mesh.SrvTable, descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
		}
	}

	return true;
}
