#pragma once


#include <ITexture.h>
#include <d3d11.h>


class TextureD3D11 : public ITexture
{

public:

	//----------------------------------
	//
	TextureD3D11();	
	
	//----------------------------------
	//
	virtual ~TextureD3D11();

	//----------------------------------
	//
	int init(void* handler, unsigned int width, unsigned int height);

	//----------------------------------
	//
	void getResourcePointers(void*& ptry, void*& ptru, void*& ptrv);
	
	//----------------------------------
	//	
	void upload(unsigned char* ych, unsigned char* uch, unsigned char* vch);
	
	//----------------------------------
	//	
	void destroy();

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
