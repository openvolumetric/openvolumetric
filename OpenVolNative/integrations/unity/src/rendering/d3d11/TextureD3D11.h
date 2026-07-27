#pragma once


#include <ITexture.h>
#include <d3d11.h>

namespace openvol::unity
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
	void getResourcePointers(void*& ptry, void*& ptru, void*& ptrv) override;
	
	/// Updates the three texture planes with one decoded YUV420P frame.
	void upload(unsigned char* ych, unsigned char* uch, unsigned char* vch) override;
	
	/// Releases all COM resources owned by the uploader.
	void destroy() override;

private:

	//
	ID3D11Device* mD3D11Device;

	//
	unsigned int mWidthY;
	unsigned int mHeightY;
	unsigned int mLengthY;

	//
	unsigned int mWidthUV;
	unsigned int mHeightUV;
	unsigned int mLengthUV;

	//
	ID3D11Texture2D* mTextures[TEXTURE_NUM];
	ID3D11ShaderResourceView* mShaderResourceView[TEXTURE_NUM];


};

} // namespace openvol::unity
