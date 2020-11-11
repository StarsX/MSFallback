//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************
#include "stdafx.h"
#include "Model.h"

#include "DXFrameworkHelper.h"

#include <fstream>
#include <unordered_set>

using namespace DirectX;
using namespace Microsoft::WRL;

namespace
{
	const D3D12_INPUT_ELEMENT_DESC c_elementDescs[Attribute::Count] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 1 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 1 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 1 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 1 },
		{ "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 1 },
	};

	const uint32_t c_sizeMap[] =
	{
		12, // Position
		12, // Normal
		8,  // TexCoord
		12, // Tangent
		12, // Bitangent
	};

	const uint32_t c_prolog = 'MSHL';

	enum FileVersion
	{
		FILE_VERSION_INITIAL = 0,
		CURRENT_FILE_VERSION = FILE_VERSION_INITIAL
	};

	struct FileHeader
	{
		uint32_t Prolog;
		uint32_t Version;

		uint32_t MeshCount;
		uint32_t AccessorCount;
		uint32_t BufferViewCount;
		uint32_t BufferSize;
	};

	struct MeshHeader
	{
		uint32_t Indices;
		uint32_t IndexSubsets;
		uint32_t Attributes[Attribute::Count];

		uint32_t Meshlets;
		uint32_t MeshletSubsets;
		uint32_t UniqueVertexIndices;
		uint32_t PrimitiveIndices;
		uint32_t CullData;
	};

	struct BufferView
	{
		uint32_t Offset;
		uint32_t Size;
	};

	struct Accessor
	{
		uint32_t BufferView;
		uint32_t Offset;
		uint32_t Size;
		uint32_t Stride;
		uint32_t Count;
	};

	uint32_t GetFormatSize(DXGI_FORMAT format)
	{ 
		switch(format)
		{
			case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
			case DXGI_FORMAT_R32G32B32_FLOAT: return 12;
			case DXGI_FORMAT_R32G32_FLOAT: return 8;
			case DXGI_FORMAT_R32_FLOAT: return 4;
			default: throw std::exception("Unimplemented type");
		}
	}
}

HRESULT Model::LoadFromFile(const wchar_t* filename)
{
	std::ifstream stream(filename, std::ios::binary);
	if (!stream.is_open())
	{
		return E_INVALIDARG;
	}

	std::vector<MeshHeader> meshes;
	std::vector<BufferView> bufferViews;
	std::vector<Accessor> accessors;

	FileHeader header;
	stream.read(reinterpret_cast<char*>(&header), sizeof(header));

	if (header.Prolog != c_prolog) return E_FAIL; // Incorrect file format.
	if (header.Version != CURRENT_FILE_VERSION) return E_FAIL; // Version mismatch between export and import serialization code.

	// Read mesh metdata
	meshes.resize(header.MeshCount);
	stream.read(reinterpret_cast<char*>(meshes.data()), meshes.size() * sizeof(meshes[0]));
	
	accessors.resize(header.AccessorCount);
	stream.read(reinterpret_cast<char*>(accessors.data()), accessors.size() * sizeof(accessors[0]));

	bufferViews.resize(header.BufferViewCount);
	stream.read(reinterpret_cast<char*>(bufferViews.data()), bufferViews.size() * sizeof(bufferViews[0]));

	m_buffer.resize(header.BufferSize);
	stream.read(reinterpret_cast<char*>(m_buffer.data()), header.BufferSize);

	char eofbyte;
	stream.read(&eofbyte, 1); // Read last byte to hit the eof bit

	assert(stream.eof()); // There's a problem if we didn't completely consume the file contents.

	stream.close();

	// Populate mesh data from binary data and metadata.
	m_meshes.resize(meshes.size());
	for (uint32_t i = 0; i < static_cast<uint32_t>(meshes.size()); ++i)
	{
		auto& meshView = meshes[i];
		auto& mesh = m_meshes[i];

		// Index data
		{
			Accessor& accessor = accessors[meshView.Indices];
			BufferView& bufferView = bufferViews[accessor.BufferView];

			mesh.IndexSize = accessor.Size;
			mesh.IndexCount = accessor.Count;

			mesh.Indices = MakeSpan(m_buffer.data() + bufferView.Offset, bufferView.Size);
		}

		// Index Subset data
		{
			Accessor& accessor = accessors[meshView.IndexSubsets];
			BufferView& bufferView = bufferViews[accessor.BufferView];

			mesh.IndexSubsets = MakeSpan(reinterpret_cast<Subset*>(m_buffer.data() + bufferView.Offset), accessor.Count);
		}

		// Vertex data & layout metadata

		// Determine the number of unique Buffer Views associated with the vertex attributes & copy vertex buffers.
		std::vector<uint32_t> vbMap;

		mesh.LayoutDesc.pInputElementDescs = mesh.LayoutElems;
		mesh.LayoutDesc.NumElements = 0;

		for (uint32_t j = 0; j < Attribute::Count; ++j)
		{
			if (meshView.Attributes[j] == -1)
				continue;

			Accessor& accessor = accessors[meshView.Attributes[j]];
			
			auto it = std::find(vbMap.begin(), vbMap.end(), accessor.BufferView);
			if (it != vbMap.end())
			{
				continue; // Already added - continue.
			}

			// New buffer view encountered; add to list and copy vertex data
			vbMap.push_back(accessor.BufferView);
			BufferView& bufferView = bufferViews[accessor.BufferView];

			Span<uint8_t> verts = MakeSpan(m_buffer.data() + bufferView.Offset, bufferView.Size);

			mesh.VertexStrides.push_back(accessor.Stride);
			mesh.Vertices.push_back(verts);
			mesh.VertexCount = static_cast<uint32_t>(verts.size()) / accessor.Stride;
		}

		 // Populate the vertex buffer metadata from accessors.
		for (uint32_t j = 0; j < Attribute::Count; ++j)
		{
			if (meshView.Attributes[j] == -1)
				continue;

			Accessor& accessor = accessors[meshView.Attributes[j]];

			// Determine which vertex buffer index holds this attribute's data
			auto it = std::find(vbMap.begin(), vbMap.end(), accessor.BufferView);

			D3D12_INPUT_ELEMENT_DESC desc = c_elementDescs[j];
			desc.InputSlot = static_cast<uint32_t>(std::distance(vbMap.begin(), it));

			mesh.LayoutElems[mesh.LayoutDesc.NumElements++] = desc;
		}

		// Meshlet data
		{
			Accessor& accessor = accessors[meshView.Meshlets];
			BufferView& bufferView = bufferViews[accessor.BufferView];

			mesh.Meshlets = MakeSpan(reinterpret_cast<Meshlet*>(m_buffer.data() + bufferView.Offset), accessor.Count);
		}

		// Meshlet Subset data
		{
			Accessor& accessor = accessors[meshView.MeshletSubsets];
			BufferView& bufferView = bufferViews[accessor.BufferView];

			mesh.MeshletSubsets = MakeSpan(reinterpret_cast<Subset*>(m_buffer.data() + bufferView.Offset), accessor.Count);
		}

		// Unique Vertex Index data
		{
			Accessor& accessor = accessors[meshView.UniqueVertexIndices];
			BufferView& bufferView = bufferViews[accessor.BufferView];

			mesh.UniqueVertexIndices = MakeSpan(m_buffer.data() + bufferView.Offset, bufferView.Size);
		}

		// Primitive Index data
		{
			Accessor& accessor = accessors[meshView.PrimitiveIndices];
			BufferView& bufferView = bufferViews[accessor.BufferView];

			mesh.PrimitiveIndices = MakeSpan(reinterpret_cast<PackedTriangle*>(m_buffer.data() + bufferView.Offset), accessor.Count);
		}

		// Cull data
		{
			Accessor& accessor = accessors[meshView.CullData];
			BufferView& bufferView = bufferViews[accessor.BufferView];

			mesh.CullingData = MakeSpan(reinterpret_cast<CullData*>(m_buffer.data() + bufferView.Offset), accessor.Count);
		}
	 }

	// Build bounding spheres for each mesh
	for (uint32_t i = 0; i < static_cast<uint32_t>(m_meshes.size()); ++i)
	{
		auto& m = m_meshes[i];

		uint32_t vbIndexPos = 0;

		// Find the index of the vertex buffer of the position attribute
		for (uint32_t j = 1; j < m.LayoutDesc.NumElements; ++j)
		{
			auto& desc = m.LayoutElems[j];
			if (strcmp(desc.SemanticName, "POSITION") == 0)
			{
				vbIndexPos = j;
				break;
			}
		}

		// Find the byte offset of the position attribute with its vertex buffer
		uint32_t positionOffset = 0;

		for (uint32_t j = 0; j < m.LayoutDesc.NumElements; ++j)
		{
			auto& desc = m.LayoutElems[j];
			if (strcmp(desc.SemanticName, "POSITION") == 0)
			{
				break;
			}

			if (desc.InputSlot == vbIndexPos)
			{
				positionOffset += GetFormatSize(m.LayoutElems[j].Format);
			}
		}

		XMFLOAT3* v0 = reinterpret_cast<XMFLOAT3*>(m.Vertices[vbIndexPos].data() + positionOffset);
		uint32_t stride = m.VertexStrides[vbIndexPos];

		BoundingSphere::CreateFromPoints(m.BoundingSphere, m.VertexCount, v0, stride);

		if (i == 0)
		{
			m_boundingSphere = m.BoundingSphere;
		}
		else
		{
			BoundingSphere::CreateMerged(m_boundingSphere, m_boundingSphere, m.BoundingSphere);
		}
	}
	
	return S_OK;
}
