#include "TextureMetal.h"

#include <Logger.h>
#include <Unity/IUnityGraphicsMetal.h>

#import <Metal/Metal.h>

#include <cstring>

namespace openvolumetric::unity
{

TextureMetal::TextureMetal()
	: m_device(nullptr),
	  m_unity_metal(nullptr),
	  m_width_y(0),
	  m_height_y(0),
	  m_row_bytes_y(0),
	  m_width_uv(0),
	  m_height_uv(0),
	  m_row_bytes_uv(0)
{
	for (unsigned int i = 0; i < TEXTURE_COUNT; ++i)
		m_textures[i] = nullptr;
}


TextureMetal::~TextureMetal()
{
	destroy();
}

int TextureMetal::init(void* handler, unsigned int width, unsigned int height)
{
	if (handler == nullptr || width == 0 || height == 0)
		return -1;

	destroy();
	m_device = handler;
	m_width_y = width;
	m_height_y = height;
	m_row_bytes_y = ((width + CPU_ALIGNMENT - 1) / CPU_ALIGNMENT) * CPU_ALIGNMENT;
	m_width_uv = width / 2;
	m_height_uv = height / 2;
	m_row_bytes_uv = m_row_bytes_y / 2;

	IUnityGraphicsMetal* unity_metal =
		static_cast<IUnityGraphicsMetal*>(handler);
	id<MTLDevice> device = unity_metal->MetalDevice();
	if (device == nil)
		return -1;
	m_unity_metal = unity_metal;
	m_device = (__bridge void*)device;

	const unsigned int widths[TEXTURE_COUNT] = {m_width_y, m_width_uv, m_width_uv};
	const unsigned int heights[TEXTURE_COUNT] = {m_height_y, m_height_uv, m_height_uv};

	for (unsigned int i = 0; i < TEXTURE_COUNT; ++i)
	{
		MTLTextureDescriptor* descriptor =
			[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatA8Unorm
			                                                 width:widths[i]
			                                                height:heights[i]
			                                             mipmapped:NO];
		descriptor.usage = MTLTextureUsageShaderRead;
		// Unity samples these textures on the GPU. Keep them private and upload
		// through Unity's command buffer so an in-flight frame is never
		// overwritten from the CPU.
		descriptor.storageMode = MTLStorageModePrivate;

		id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
		if (texture == nil)
		{
			LOG("TextureMetal::init - failed to create texture %u", i);
			destroy();
			return -1;
		}
		m_textures[i] = (__bridge_retained void*)texture;
	}

	return 1;
}

void TextureMetal::get_resource_pointers(void*& ptry, void*& ptru, void*& ptrv)
{
	ptry = m_textures[0];
	ptru = m_textures[1];
	ptrv = m_textures[2];
}

void TextureMetal::upload(unsigned char* ych, unsigned char* uch, unsigned char* vch)
{
	if (m_device == nullptr || ych == nullptr || uch == nullptr || vch == nullptr)
		return;

	const unsigned char* data[TEXTURE_COUNT] = {ych, uch, vch};
	const unsigned int widths[TEXTURE_COUNT] = {m_width_y, m_width_uv, m_width_uv};
	const unsigned int heights[TEXTURE_COUNT] = {m_height_y, m_height_uv, m_height_uv};
	const unsigned int row_bytes[TEXTURE_COUNT] = {m_row_bytes_y, m_row_bytes_uv, m_row_bytes_uv};

	IUnityGraphicsMetal* unity_metal =
		static_cast<IUnityGraphicsMetal*>(m_unity_metal);
	id<MTLDevice> device = (__bridge id<MTLDevice>)m_device;
	if (unity_metal == nullptr || device == nil)
		return;

	// Allocate and populate every plane before recording commands so allocation
	// failure cannot publish a texture containing a mixture of old/new planes.
	id<MTLBuffer> staging[TEXTURE_COUNT]{};
	unsigned int staging_row_bytes[TEXTURE_COUNT]{};
	NSUInteger staging_lengths[TEXTURE_COUNT]{};
	for (unsigned int i = 0; i < TEXTURE_COUNT; ++i)
	{
		staging_row_bytes[i] = ((widths[i] + 255u) / 256u) * 256u;
		staging_lengths[i] =
			static_cast<NSUInteger>(staging_row_bytes[i]) * heights[i];
		staging[i] = [device newBufferWithLength:staging_lengths[i]
		                                options:MTLResourceStorageModeShared];
		if (staging[i] == nil)
		{
			LOG("TextureMetal::upload - failed to allocate plane %u staging", i);
			return;
		}
		unsigned char* destination =
			static_cast<unsigned char*>(staging[i].contents);
		for (unsigned int row = 0; row < heights[i]; ++row)
			std::memcpy(
				destination + row * staging_row_bytes[i],
				data[i] + row * row_bytes[i],
				widths[i]);
	}

	// A blit encoder cannot coexist with Unity's current render encoder.
	unity_metal->EndCurrentCommandEncoder();
	id<MTLCommandBuffer> command_buffer =
		(id<MTLCommandBuffer>)unity_metal->CurrentCommandBuffer();
	if (command_buffer == nil)
	{
		LOG("TextureMetal::upload - Unity command buffer is unavailable");
		return;
	}

	id<MTLBlitCommandEncoder> blit = [command_buffer blitCommandEncoder];
	if (blit == nil)
	{
		LOG("TextureMetal::upload - failed to create blit encoder");
		return;
	}

	for (unsigned int i = 0; i < TEXTURE_COUNT; ++i)
	{
		id<MTLTexture> texture = (__bridge id<MTLTexture>)m_textures[i];
		[blit copyFromBuffer:staging[i]
		       sourceOffset:0
		  sourceBytesPerRow:staging_row_bytes[i]
		sourceBytesPerImage:staging_lengths[i]
		         sourceSize:MTLSizeMake(widths[i], heights[i], 1)
		          toTexture:texture
		   destinationSlice:0
		   destinationLevel:0
		  destinationOrigin:MTLOriginMake(0, 0, 0)];
	}
	[blit endEncoding];
}

void TextureMetal::destroy()
{
	for (unsigned int i = 0; i < TEXTURE_COUNT; ++i)
	{
		if (m_textures[i] != nullptr)
		{
			CFRelease(m_textures[i]);
			m_textures[i] = nullptr;
		}
	}
	m_device = nullptr;
	m_unity_metal = nullptr;
}

} // namespace openvolumetric::unity
