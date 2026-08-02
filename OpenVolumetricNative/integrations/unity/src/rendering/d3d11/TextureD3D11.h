#pragma once


#include <ITexture.h>
#include <d3d11.h>

namespace openvolumetric::unity
{

/// Owns three D3D11 single-channel textures exposed to Unity.
class TextureD3D11 : public ITexture
{
public:
	/// Constructs an empty texture set.
	TextureD3D11();	
	
	/// Releases D3D11 texture and view resources.
	~TextureD3D11() override;

	/// Allocates luma and half-resolution chroma textures.
	int init(void* handler, unsigned int width, unsigned int height) override;

	/// Returns shader-resource views for Unity external texture wrapping.
	void get_resource_pointers(void*& y, void*& u, void*& v) override;
	
	/// Updates the three texture planes with one decoded YUV420P frame.
	void upload(unsigned char* ych, unsigned char* uch, unsigned char* vch) override;
	
	/// Releases all COM resources owned by the uploader.
	void destroy() override;

private:
	/// Borrowed D3D11 device supplied by Unity.
	ID3D11Device* m_device = nullptr;

	/// Source-plane dimensions and tightly packed byte counts.
	unsigned int m_width_y = 0;
	unsigned int m_height_y = 0;
	unsigned int m_length_y = 0;
	unsigned int m_width_uv = 0;
	unsigned int m_height_uv = 0;
	unsigned int m_length_uv = 0;

	/// Plugin-owned textures and views released by destroy().
	ID3D11Texture2D* m_textures[TEXTURE_COUNT]{};
	ID3D11ShaderResourceView* m_shader_resource_views[TEXTURE_COUNT]{};
};

} // namespace openvolumetric::unity
