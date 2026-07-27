#pragma once

#include <ITexture.h>

namespace openvol::unity
{

/// Owns three single-channel Metal textures used as Unity external textures.
class TextureMetal : public ITexture
{
public:
	/// Constructs an empty texture set.
	TextureMetal();
	/// Releases uploader-owned Metal textures.
	~TextureMetal() override;

	/// Allocates luma and half-resolution chroma textures.
	int init(void* handler, unsigned int width, unsigned int height) override;
	/// Exposes native MTLTexture pointers for Unity wrapping.
	void getResourcePointers(void*& ptry, void*& ptru, void*& ptrv) override;
	/// Replaces all three planes with one decoded YUV420P frame.
	void upload(unsigned char* ych, unsigned char* uch, unsigned char* vch) override;
	/// Releases textures and invalidates borrowed interface pointers.
	void destroy() override;

private:
	void* m_device;
	void* m_unity_metal;
	void* m_textures[TEXTURE_NUM];
	unsigned int m_width_y;
	unsigned int m_height_y;
	unsigned int m_row_bytes_y;
	unsigned int m_width_uv;
	unsigned int m_height_uv;
	unsigned int m_row_bytes_uv;
};

} // namespace openvol::unity
