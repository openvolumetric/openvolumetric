#pragma once

#include <IAVDecoder.h>
#include <ITexture.h>
#include <IGeometryDecoder.h>
#include <IMeshBuffer.h>
#include <Mesh.h>

#include <cstddef>
#include <cstdint>
#include <atomic>


/// Coordinates engine-neutral decoding with an engine-specific upload backend.
///
/// Concrete platform implementations create the FFmpeg and Draco decoders,
/// texture uploader, and mesh-buffer uploader. Unity calls render() on its
/// render thread after selecting the desired presentation frame.
class IVolumetricVideo
{

public:

	//--------------------------------------------------------
	// default constructor
	//--------------------------------------------------------
	IVolumetricVideo() : m_id(-1), m_avdecoder(NULL), m_texture(NULL), m_geometrydecoder(NULL), m_meshbuffer(NULL), m_geometry_generation(0), m_last_presented_time(-1.0) {};


	//--------------------------------------------------------
	// constructor with instance id
	//--------------------------------------------------------
	IVolumetricVideo(int id) :m_id(id), m_avdecoder(NULL), m_texture(NULL), m_geometrydecoder(NULL), m_meshbuffer(NULL), m_geometry_generation(0), m_last_presented_time(-1.0) {};
	
	
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
	// Set the engine-clock presentation target in seconds.
	//--------------------------------------------------------
	void set_presentation_time(double time) { m_presentation_time = time; }


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

	double get_last_presented_time() const
	{
		return m_last_presented_time.load(std::memory_order_acquire);
	}

protected:

	void set_last_presented_time(double time)
	{
		m_last_presented_time.store(time, std::memory_order_release);
	}

	/// Moves queued vvge payloads from the media decoder into the Draco worker.
	/// A look-ahead window keeps geometry ready for upcoming render frames.
	bool submit_embedded_geometry(double presentation_time);

	/// Blocks a running pipeline briefly after seek until texture and geometry
	/// for the target timestamp are both decoded.
	bool prepare_presentation(double presentation_time);


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
	double m_presentation_time = 0.0;

	// Identifies which seek/loop pass currently owns queued Draco work.
	std::uint64_t m_geometry_generation;
	std::atomic<double> m_last_presented_time;

};
