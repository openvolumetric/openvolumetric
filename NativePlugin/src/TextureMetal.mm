#include "TextureMetal.h"

#include <Logger.h>

#import <Metal/Metal.h>

#include <cstring>

TextureMetal::TextureMetal()
	: m_device(nullptr),
	  m_width_y(0),
	  m_height_y(0),
	  m_row_bytes_y(0),
	  m_width_uv(0),
	  m_height_uv(0),
	  m_row_bytes_uv(0)
{
	for (unsigned int i = 0; i < TEXTURE_NUM; ++i)
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
	m_row_bytes_y = ((width + CPU_ALIGMENT - 1) / CPU_ALIGMENT) * CPU_ALIGMENT;
	m_width_uv = width / 2;
	m_height_uv = height / 2;
	m_row_bytes_uv = m_row_bytes_y / 2;

	id<MTLDevice> device = (__bridge id<MTLDevice>)handler;
	const unsigned int widths[TEXTURE_NUM] = {m_width_y, m_width_uv, m_width_uv};
	const unsigned int heights[TEXTURE_NUM] = {m_height_y, m_height_uv, m_height_uv};

	for (unsigned int i = 0; i < TEXTURE_NUM; ++i)
	{
		MTLTextureDescriptor* descriptor =
			[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatA8Unorm
			                                                 width:widths[i]
			                                                height:heights[i]
			                                             mipmapped:NO];
		descriptor.usage = MTLTextureUsageShaderRead;
		descriptor.storageMode = MTLStorageModeShared;

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

void TextureMetal::getResourcePointers(void*& ptry, void*& ptru, void*& ptrv)
{
	ptry = m_textures[0];
	ptru = m_textures[1];
	ptrv = m_textures[2];
}

void TextureMetal::upload(unsigned char* ych, unsigned char* uch, unsigned char* vch)
{
	if (m_device == nullptr || ych == nullptr || uch == nullptr || vch == nullptr)
		return;

	const unsigned char* data[TEXTURE_NUM] = {ych, uch, vch};
	const unsigned int widths[TEXTURE_NUM] = {m_width_y, m_width_uv, m_width_uv};
	const unsigned int heights[TEXTURE_NUM] = {m_height_y, m_height_uv, m_height_uv};
	const unsigned int row_bytes[TEXTURE_NUM] = {m_row_bytes_y, m_row_bytes_uv, m_row_bytes_uv};

	for (unsigned int i = 0; i < TEXTURE_NUM; ++i)
	{
		id<MTLTexture> texture = (__bridge id<MTLTexture>)m_textures[i];
		MTLRegion region = MTLRegionMake2D(0, 0, widths[i], heights[i]);
		[texture replaceRegion:region
		          mipmapLevel:0
		            withBytes:data[i]
		          bytesPerRow:row_bytes[i]];
	}
}

void TextureMetal::destroy()
{
	for (unsigned int i = 0; i < TEXTURE_NUM; ++i)
	{
		if (m_textures[i] != nullptr)
		{
			CFRelease(m_textures[i]);
			m_textures[i] = nullptr;
		}
	}
	m_device = nullptr;
}
