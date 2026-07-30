#pragma once

#include <Unity/IUnityInterface.h>

#define OPENVOLUMETRIC_API UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API

// C ABI consumed by OpenVolumetricDecoder.cs. Player instances are
// identified by the integer returned from openvolumetric_init().
// Resource setup runs on Unity's main thread. Frame uploads happen through
// GetRenderEventFunc() on Unity's render thread, using the texture and mesh
// resources registered through this interface.
extern "C" 
{
	/// Opens the optional desktop console used for native diagnostics.
	OPENVOLUMETRIC_API void	openvolumetric_open_external_console();

	/// Closes a diagnostic console previously opened by this plugin.
	OPENVOLUMETRIC_API void	openvolumetric_close_external_console();

	/// Creates a graphics-backend instance and writes its stable identifier.
	/// Returns 1 on success and -1 when the active graphics API is unsupported.
	OPENVOLUMETRIC_API int	openvolumetric_init(int& id);
	
	/// Destroys one instance and releases all of its native resources.
	OPENVOLUMETRIC_API void	openvolumetric_quit(int id);

	/// Sets the desired presentation time from the engine playback clock.
	OPENVOLUMETRIC_API void openvolumetric_set_time(int id, double time);


	/// Starts media and geometry worker threads for one initialized instance.
	OPENVOLUMETRIC_API int	openvolumetric_start_decoding(int id);

	/// Stops worker threads while preserving initialized graphics resources.
	OPENVOLUMETRIC_API int	openvolumetric_stop_decoding(int id);

	/// Seeks the unified media pipeline to time in seconds.
	OPENVOLUMETRIC_API int	openvolumetric_seek(int id, double time);


	/// Opens a combined MP4. Fails if video or vvge geometry is absent.
	OPENVOLUMETRIC_API int	openvolumetric_load_video(int id, const char* filepath);

	/// Returns a borrowed human-readable error string for the instance.
	OPENVOLUMETRIC_API const char* openvolumetric_get_last_error(int id);
	/// Returns the timestamp most recently uploaded by the render thread.
	OPENVOLUMETRIC_API double openvolumetric_get_last_presented_time(int id);
	/// Retrieves input transport, cache, download, and request counters.
	OPENVOLUMETRIC_API int openvolumetric_get_buffer_details(
		int id,
		int& state,
		int& remote,
		long long& resource_size_bytes,
		unsigned long long& cached_bytes,
		unsigned long long& downloaded_bytes,
		unsigned long long& request_count,
		unsigned long long& recovery_count);

	/// Retrieves decoded dimensions, nominal FPS, and duration in seconds.
	OPENVOLUMETRIC_API int	openvolumetric_get_video_details(int id, int& width, int& height, double& fps, double& duration);

	/// Retrieves decoded PCM layout. Returns 0 when audio is absent.
	OPENVOLUMETRIC_API int	openvolumetric_get_audio_details(int id, int& sample_rate, int& channels);

	/// Arms native DSP audio at an absolute Unity sample tick.
	OPENVOLUMETRIC_API int openvolumetric_schedule_dsp_audio(
		int id,
		unsigned long long dsp_start_tick,
		double media_start_time);

	/// Stops native DSP audio consumption before pause, seek, or destruction.
	OPENVOLUMETRIC_API void openvolumetric_stop_dsp_audio(int id);

	/// Returns the media time most recently processed by Unity's DSP callback.
	OPENVOLUMETRIC_API double openvolumetric_get_dsp_audio_time();

	/// Retrieves the PCM read timestamp, queued duration, and underrun count.
	OPENVOLUMETRIC_API int openvolumetric_get_audio_buffer_details(
		int id,
		double& read_time,
		double& buffered_duration,
		unsigned long long& underrun_count);

	/// Returns platform texture handles used to construct Unity textures.
	OPENVOLUMETRIC_API int	openvolumetric_get_texture_pointers(int id, void*& yPointer, void*& uPointer, void*& vPointer);
	/// Registers the handles Unity exposes after creating external textures.
	OPENVOLUMETRIC_API int openvolumetric_register_texture_pointers(
		int id, void* yPointer, void* uPointer, void* vPointer);


	/// Registers Unity-owned index and vertex buffers for native uploads.
	OPENVOLUMETRIC_API int	openvolumetric_set_mesh_pointer(int id, void* indexBufferHandle, int index_size, void* vertexBufferHandle, int vertex_size);

}
