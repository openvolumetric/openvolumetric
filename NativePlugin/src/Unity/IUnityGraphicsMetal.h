#pragma once

#include "IUnityInterface.h"

#ifndef __OBJC__
#error Metal Unity plugins must be compiled as Objective-C++.
#endif

#ifndef __clang__
#error Metal Unity plugins require Clang.
#endif

@class NSBundle;
@class MTLRenderPassDescriptor;
@protocol MTLDevice;

UNITY_DECLARE_INTERFACE(IUnityGraphicsMetal)
{
	NSBundle* (UNITY_INTERFACE_API * MetalBundle)();
	id<MTLDevice> (UNITY_INTERFACE_API * MetalDevice)();
	id (UNITY_INTERFACE_API * CurrentCommandBuffer)();
	id (UNITY_INTERFACE_API * CurrentCommandEncoder)();
	void (UNITY_INTERFACE_API * EndCurrentCommandEncoder)();
	MTLRenderPassDescriptor* (UNITY_INTERFACE_API * CurrentRenderPassDescriptor)();
	UnityRenderBuffer (UNITY_INTERFACE_API * RenderBufferFromHandle)(void* bufferHandle);
	id (UNITY_INTERFACE_API * TextureFromRenderBuffer)(UnityRenderBuffer buffer);
	id (UNITY_INTERFACE_API * AAResolvedTextureFromRenderBuffer)(UnityRenderBuffer buffer);
	id (UNITY_INTERFACE_API * StencilTextureFromRenderBuffer)(UnityRenderBuffer buffer);
};

UNITY_REGISTER_INTERFACE_GUID(
	0x992C8EAEA95811E5ULL,
	0x9A62C4B5B9876117ULL,
	IUnityGraphicsMetal)
