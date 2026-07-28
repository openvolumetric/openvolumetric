#pragma once

#include <IAVDecoder.h>
#include <ITexture.h>
#include <IGeometryDecoder.h>
#include <IMeshBuffer.h>
#include <Mesh.h>

#include <cstddef>
#include <cstdint>
#include <atomic>

namespace openvolumetric
{


/// Coordinates engine-neutral decoding with an engine-specific upload backend.
///
/// Concrete platform implementations create the FFmpeg and Draco decoders,
/// texture uploader, and mesh-buffer uploader. Unity calls render() on its
/// render thread after selecting the desired presentation frame.
class IVolumetricVideo
{

public:
	/// Constructs an unattached coordinator for tests or deferred assignment.
	IVolumetricVideo() : m_id(-1), m_avdecoder(NULL), m_texture(NULL), m_geometrydecoder(NULL), m_meshbuffer(NULL), m_geometry_generation(0), m_last_presented_time(-1.0) {};

	/// Constructs a coordinator with the stable identifier exposed by the C API.
	IVolumetricVideo(int id) :m_id(id), m_avdecoder(NULL), m_texture(NULL), m_geometrydecoder(NULL), m_meshbuffer(NULL), m_geometry_generation(0), m_last_presented_time(-1.0) {};
	
	/// Concrete backends call destroy() before base destruction.
	virtual ~IVolumetricVideo() {};

	/// Returns the stable decoder identifier used by exported API calls.
	int get_id() const { return m_id; }

	/// Returns the owned media decoder; valid after backend construction.
	IAVDecoder* get_avdecoder_ptr() { return m_avdecoder; }

	/// Returns the owned texture uploader; valid after backend construction.
	ITexture* get_texture_ptr() { return m_texture; }

	/// Returns the owned asynchronous geometry decoder.
	IGeometryDecoder* get_geometrydecoder_ptr() { return m_geometrydecoder; }

	/// Returns the owned engine-specific mesh uploader.
	IMeshBuffer* get_meshbuffer() { return m_meshbuffer; }

	/// Sets the engine-clock presentation target in seconds.
	void set_presentation_time(double time) { m_presentation_time = time; }

	/// Starts the media and geometry workers for this backend.
	virtual int start() = 0;

	/// Stops workers without releasing registered graphics resources.
	virtual int stop() = 0;

	/// Seeks every stream and prepares a complete presentation near time.
	virtual int seek(double time) = 0;

	/// Selects matching texture/geometry and uploads them on the render thread.
	virtual int render() = 0;

	/// Stops decoding and releases all backend-owned resources.
	virtual void destroy() = 0;

	/// Returns the PTS most recently uploaded successfully, or -1 before one.
	double get_last_presented_time() const
	{
		return m_last_presented_time.load(std::memory_order_acquire);
	}

protected:
	/// Publishes the completed presentation time across the C API boundary.
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


	/// Stable identifier assigned by the exported Unity API.
	int					m_id;

	/// Owned combined MP4 decoder.
	IAVDecoder*			m_avdecoder;

	/// Owned graphics-backend texture uploader.
	ITexture*			m_texture;

	/// Owned Draco worker and decoded-mesh queue.
	IGeometryDecoder*	m_geometrydecoder;

	/// Owned graphics-backend mesh uploader.
	IMeshBuffer*		m_meshbuffer;

	/// Current presentation target supplied by the engine clock.
	double m_presentation_time = 0.0;

	// Identifies which seek/loop pass currently owns queued Draco work.
	std::uint64_t m_geometry_generation;
	std::atomic<double> m_last_presented_time;

};

} // namespace openvolumetric
