#pragma once

#include <d3d12.h>
#include <dxgi.h>
#include <Unity/IUnityGraphicsD3D12.h>

namespace openvolumetric::unity
{
struct D3D12UnityContext
{
	ID3D12Device* device = nullptr;
	IUnityGraphicsD3D12v8* unity = nullptr;
};
}
