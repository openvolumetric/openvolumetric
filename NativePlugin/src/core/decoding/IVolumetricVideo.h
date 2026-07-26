#pragma once

#include <IAVDecoder.h>
#include <ITexture.h>
#include <IGeometryDecoder.h>
#include <IMeshBuffer.h>
#include <Mesh.h>

#include <cstddef>


//
//
//
class IVolumetricVideo
{

public:

	//--------------------------------------------------------
	// default constructor
	//--------------------------------------------------------
	IVolumetricVideo() : m_id(-1), m_unity_time(0.0), m_frame_index(-1), m_avdecoder(NULL), m_texture(NULL), m_geometrydecoder(NULL), m_meshbuffer(NULL) {};


	//--------------------------------------------------------
	// constructor with instance id
	//--------------------------------------------------------
	IVolumetricVideo(int id) :m_id(id), m_unity_time(0.0), m_frame_index(-1), m_avdecoder(NULL), m_texture(NULL), m_geometrydecoder(NULL), m_meshbuffer(NULL) {};
	
	
	//--------------------------------------------------------
	// destructor
	//--------------------------------------------------------
	virtual ~IVolumetricVideo() {};


	//--------------------------------------------------------
	// return the instance id
	//--------------------------------------------------------
	int get_id() { return m_id; }


	//--------------------------------------------------------
	//  get Texture pointers
	//--------------------------------------------------------
	IAVDecoder* get_avdecoder_ptr() { return m_avdecoder; }


	//--------------------------------------------------------
	// get Texture pointers
	//--------------------------------------------------------
	ITexture* get_texture_ptr() { return m_texture; }


	//--------------------------------------------------------
	//  get Geometry Decoder pointer
	//--------------------------------------------------------
	IGeometryDecoder* get_geometrydecoder_ptr() { return m_geometrydecoder; }


	//--------------------------------------------------------
	//  get Mesh Buffer pointer
	//--------------------------------------------------------
	IMeshBuffer* get_meshbuffer() { return m_meshbuffer; }


	//--------------------------------------------------------
	// set_global_time
	//--------------------------------------------------------
	void set_unity_time(double unity_time) { m_unity_time = unity_time; }


	//--------------------------------------------------------
	// set frame index
	//--------------------------------------------------------
	void set_frame_index(int frame_index) { m_frame_index = frame_index; }


	//--------------------------------------------------------
	// function to implement: start decoder
	//--------------------------------------------------------
	virtual int start() = 0;


	//--------------------------------------------------------
	// function to implement: stop decoder
	//--------------------------------------------------------
	virtual int stop() = 0;


	//--------------------------------------------------------
	// function to implement: set frame
	//--------------------------------------------------------
	virtual int seek(double time) = 0;


	//--------------------------------------------------------
	// function to implement: render
	//--------------------------------------------------------
	virtual int render() = 0;

	//--------------------------------------------------------
	// function to implement: destroy - memory cleanup etc
	//--------------------------------------------------------
	virtual void destroy() = 0;


protected:


	//--------------------------------------------------------
	// instance id 
	//--------------------------------------------------------
	int					m_id;


	//--------------------------------------------------------
	// AV decoder
	//--------------------------------------------------------
	IAVDecoder*			m_avdecoder;


	//--------------------------------------------------------
	// Texture
	//--------------------------------------------------------
	ITexture*			m_texture;


	//--------------------------------------------------------
	// Geometry Decoder
	//--------------------------------------------------------
	IGeometryDecoder*	m_geometrydecoder;


	//--------------------------------------------------------
	// Mesh Buffer
	//--------------------------------------------------------
	IMeshBuffer*		m_meshbuffer;


	//--------------------------------------------------------
	// Unity Time
	//--------------------------------------------------------
	double m_unity_time;


	//--------------------------------------------------------
	// Frame Index
	//--------------------------------------------------------
	int m_frame_index;

};
