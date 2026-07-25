#pragma once

#include <ITexture.h>

class TextureMetal : public ITexture
{
public:
	TextureMetal();
	~TextureMetal() override;

	int init(void* handler, unsigned int width, unsigned int height) override;
	void getResourcePointers(void*& ptry, void*& ptru, void*& ptrv) override;
	void upload(unsigned char* ych, unsigned char* uch, unsigned char* vch) override;
	void destroy() override;

private:
	void* m_device;
	void* m_textures[TEXTURE_NUM];
	unsigned int m_width_y;
	unsigned int m_height_y;
	unsigned int m_row_bytes_y;
	unsigned int m_width_uv;
	unsigned int m_height_uv;
	unsigned int m_row_bytes_uv;
};
