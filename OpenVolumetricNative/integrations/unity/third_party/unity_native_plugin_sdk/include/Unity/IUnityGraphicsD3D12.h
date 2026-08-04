#pragma once

#include "IUnityInterface.h"

struct RenderSurfaceBase;
typedef struct RenderSurfaceBase* UnityRenderBuffer;

typedef struct UnityGraphicsD3D12RecordingState
{
	ID3D12GraphicsCommandList* commandList;
} UnityGraphicsD3D12RecordingState;

typedef enum UnityD3D12GraphicsQueueAccess
{
	kUnityD3D12GraphicsQueueAccess_DontCare,
	kUnityD3D12GraphicsQueueAccess_Allow,
} UnityD3D12GraphicsQueueAccess;

typedef struct UnityD3D12PluginEventConfig
{
	UnityD3D12GraphicsQueueAccess graphicsQueueAccess;
	UINT32 flags;
	bool ensureActiveRenderTextureIsBound;
} UnityD3D12PluginEventConfig;

UNITY_DECLARE_INTERFACE(IUnityGraphicsD3D12v8)
{
	ID3D12Device* (UNITY_INTERFACE_API * GetDevice)();
	IDXGISwapChain* (UNITY_INTERFACE_API * GetSwapChain)();
	UINT32 (UNITY_INTERFACE_API * GetSyncInterval)();
	UINT (UNITY_INTERFACE_API * GetPresentFlags)();
	ID3D12Fence* (UNITY_INTERFACE_API * GetFrameFence)();
	UINT64 (UNITY_INTERFACE_API * GetNextFrameFenceValue)();
	UINT64 (UNITY_INTERFACE_API * ExecuteCommandList)(ID3D12GraphicsCommandList*, int, void*);
	void (UNITY_INTERFACE_API * SetPhysicalVideoMemoryControlValues)(const void*);
	ID3D12CommandQueue* (UNITY_INTERFACE_API * GetCommandQueue)();
	ID3D12Resource* (UNITY_INTERFACE_API * TextureFromRenderBuffer)(UnityRenderBuffer);
	ID3D12Resource* (UNITY_INTERFACE_API * TextureFromNativeTexture)(UnityTextureID);
	void (UNITY_INTERFACE_API * ConfigureEvent)(int, const UnityD3D12PluginEventConfig*);
	bool (UNITY_INTERFACE_API * CommandRecordingState)(UnityGraphicsD3D12RecordingState*);
	void (UNITY_INTERFACE_API * RequestResourceState)(ID3D12Resource*, D3D12_RESOURCE_STATES);
	void (UNITY_INTERFACE_API * NotifyResourceState)(ID3D12Resource*, D3D12_RESOURCE_STATES, bool);
};
UNITY_REGISTER_INTERFACE_GUID(0x9d303045d00d4cfdULL, 0x8febb42968b423b6ULL, IUnityGraphicsD3D12v8)
