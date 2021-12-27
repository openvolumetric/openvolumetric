#pragma once

#include <IAVDecoder.h>
#include <ITexture.h>
#include <IGeometryDecoder.h>

#include <cstddef>


//
//
//
class IVolumetricVideo
{

public:

	//--------------------------------------------------------
	// default constructor
	IVolumetricVideo(): m_id(-1), m_avdecoder(NULL), m_texture(NULL), m_geometrydecoder(NULL){};


	//--------------------------------------------------------
	// constructor with instance id
	IVolumetricVideo(int id) :m_id(id),  m_avdecoder(NULL), m_texture(NULL), m_geometrydecoder(NULL) {};
	
	
	//--------------------------------------------------------
	// destructor
	~IVolumetricVideo() {};

	//--------------------------------------------------------
	// return the instance id
	int get_id() { return m_id; }


	//--------------------------------------------------------
	// function to implement: get Texture pointers
	IAVDecoder* get_avdecoder_ptr() { return m_avdecoder; }


	//--------------------------------------------------------
	// function to implement: get Texture pointers
	ITexture* get_texture_ptr() { return m_texture; }

	//--------------------------------------------------------
	//
	virtual int start() = 0;


	//--------------------------------------------------------
	//
	virtual int stop() = 0;


	//--------------------------------------------------------
	// function to implement: set frame
	virtual int seek(double time) = 0;


	//--------------------------------------------------------
	// function to implement: render
	//
	virtual int render() = 0;
	




protected:

	//--------------------------------------------------------
	// instance id 
	int					m_id;

	//--------------------------------------------------------
	// AV decoder
	IAVDecoder*			m_avdecoder;

	//--------------------------------------------------------
	// Texture
	ITexture*			m_texture;

	//--------------------------------------------------------
	// Geometry Decoder
	IGeometryDecoder*	m_geometrydecoder;
	
};
