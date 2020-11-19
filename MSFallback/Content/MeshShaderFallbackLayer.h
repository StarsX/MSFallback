//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "Ultimate/XUSGUltimate.h"

class MeshShaderFallbackLayer
{
public:
	class PipelineLayout
	{
	public:
		bool IsValid(bool isMSSupported) const;
	//private:
		friend MeshShaderFallbackLayer;
		XUSG::PipelineLayout m_native;
		XUSG::PipelineLayout m_as;
		XUSG::PipelineLayout m_ms;
		XUSG::PipelineLayout m_vs;
	};

	class Pipeline
	{
	public:
		bool IsValid(bool isMSSupported) const;
	//private:
		friend MeshShaderFallbackLayer;
		XUSG::Pipeline m_native;
		XUSG::Pipeline m_as;
		XUSG::Pipeline m_ms;
		XUSG::Pipeline m_vs;
	};

	MeshShaderFallbackLayer(const XUSG::Device& device, bool isMSSupported);
	virtual ~MeshShaderFallbackLayer();

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

protected:
	XUSG::Device	m_device;
	bool			m_isMSSupported;
};
