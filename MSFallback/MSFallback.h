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

#pragma once

#include "DXFramework.h"
#include "StepTimer.h"
#include "Renderer.h"

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().

class MSFallback : public DXFramework
{
public:
	MSFallback(uint32_t width, uint32_t height, std::wstring name);
	virtual ~MSFallback();

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();

	virtual void OnKeyUp(uint8_t /*key*/);
	virtual void OnLButtonDown(float posX, float posY);
	virtual void OnLButtonUp(float posX, float posY);
	virtual void OnMouseMove(float posX, float posY);
	virtual void OnMouseWheel(float deltaZ, float posX, float posY);
	virtual void OnMouseLeave();

	virtual void ParseCommandLineArgs(wchar_t* argv[], int argc);

private:
	enum DeviceType : uint8_t
	{
		DEVICE_DISCRETE,
		DEVICE_UMA,
		DEVICE_WARP
	};

	static const auto FrameCount = Renderer::FrameCount;

	// Pipeline objects.
	XUSG::DescriptorTableLib::sptr	m_descriptorTableLib;

	XUSG::Viewport					m_viewport;
	XUSG::RectRange					m_scissorRect;

	XUSG::SwapChain::uptr			m_swapChain;
	XUSG::CommandAllocator::uptr	m_commandAllocators[FrameCount];
	XUSG::CommandQueue::uptr		m_commandQueue;

	bool m_isMSSupported;

	XUSG::Device::uptr m_device;
	XUSG::RenderTarget::uptr m_renderTargets[FrameCount];
	XUSG::Ultimate::CommandList::uptr m_commandList;

	// App resources.
	std::unique_ptr<Renderer> m_renderer;
	DirectX::XMFLOAT4X4	m_proj;
	DirectX::XMFLOAT4X4	m_view;
	DirectX::XMFLOAT3	m_focusPt;
	DirectX::XMFLOAT3	m_eyePt;

	// Synchronization objects.
	uint32_t	m_frameIndex;
	HANDLE		m_fenceEvent;
	XUSG::Fence::uptr m_fence;
	uint64_t	m_fenceValues[FrameCount];

	// Application state
	DeviceType	m_deviceType;
	StepTimer	m_timer;
	bool		m_useMeshShader;
	bool		m_useDebugCamera;
	bool		m_showFPS;
	bool		m_pausing;

	// User camera interactions
	bool m_tracking;
	XMFLOAT2 m_mousePt;

	// User external settings
	static const uint32_t MODEL_COUNT = 1;
	std::wstring m_modelFilenames[MODEL_COUNT];
	Renderer::ObjectDef m_objDefs[MODEL_COUNT];

	// Screen-shot helpers and state
	XUSG::Buffer::uptr	m_readBuffer;
	uint32_t			m_rowPitch;
	uint8_t				m_screenShot;

	void LoadPipeline();
	void LoadAssets();
	void PopulateCommandList();
	void WaitForGpu();
	void MoveToNextFrame();
	void SaveImage(char const* fileName, XUSG::Buffer* pImageBuffer,
		uint32_t w, uint32_t h, uint32_t rowPitch, uint8_t comp = 3);
	double CalculateFrameStats(float* fTimeStep = nullptr);
};
