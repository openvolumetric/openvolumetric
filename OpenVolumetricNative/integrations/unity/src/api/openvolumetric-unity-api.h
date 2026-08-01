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
	/// Retrieves the vertex centroid of the most recently uploaded geometry.
	OPENVOLUMETRIC_API int openvolumetric_get_geometry_centroid(
		int id,
		float& x,
		float& y,
		float& z);
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
	/// Retrieves fragmented-input scheduler and bounded-cache progress.
	OPENVOLUMETRIC_API int openvolumetric_get_fragment_details(
		int id,
		int& fragmented,
		long long& active_fragment,
		unsigned long long& fragment_count,
		unsigned long long& cached_fragment_count);

	/// Parses an adaptive manifest and selects Auto (0), Low (1), or High (2).
	OPENVOLUMETRIC_API int openvolumetric_select_adaptive_representation(
		const char* manifest_json,
		const char* manifest_location,
		int quality);
	/// Loads a local or HTTP manifest and applies the same startup selection.
	OPENVOLUMETRIC_API int openvolumetric_load_adaptive_representation(
		const char* manifest_location,
		int quality);
	/// Loads and selects while applying device limits to Auto quality.
	OPENVOLUMETRIC_API int openvolumetric_load_adaptive_representation_with_capabilities(
		const char* manifest_location,
		int quality,
		unsigned int maximum_texture_width,
		unsigned int maximum_texture_height,
		unsigned long long maximum_texture_bitrate,
		unsigned long long maximum_geometry_bitrate,
		unsigned long long maximum_bandwidth);
	/// Parses and selects while applying device limits to Auto quality.
	OPENVOLUMETRIC_API int openvolumetric_select_adaptive_representation_with_capabilities(
		const char* manifest_json,
		const char* manifest_location,
		int quality,
		unsigned int maximum_texture_width,
		unsigned int maximum_texture_height,
		unsigned long long maximum_texture_bitrate,
		unsigned long long maximum_geometry_bitrate,
		unsigned long long maximum_bandwidth);
	/// Returns the selected representation's URI exactly as stored in JSON.
	OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_resource_uri();
	/// Returns the selected resource resolved against the manifest location.
	OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_resource();
	/// Returns the selected representation identifier.
	OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_representation_id();
	/// Returns the bounded HTTP startup probe result in bits per second.
	OPENVOLUMETRIC_API unsigned long long openvolumetric_get_adaptive_throughput_bps();
	/// Returns a human-readable explanation of the startup selection.
	OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_decision_reason();
	/// Returns the number of capability-eligible representations, lowest first.
	OPENVOLUMETRIC_API unsigned long long openvolumetric_get_adaptive_representation_count();
	/// Returns an eligible representation identifier by ascending bandwidth.
	OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_representation_id_at(
		unsigned long long index);
	/// Returns an eligible resolved resource by ascending bandwidth.
	OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_resource_at(
		unsigned long long index);
	OPENVOLUMETRIC_API unsigned long long openvolumetric_get_adaptive_bandwidth_at(
		unsigned long long index);
	/// Returns the shared adaptive fragment duration in seconds.
	OPENVOLUMETRIC_API double openvolumetric_get_adaptive_segment_duration();
	/// Associates the initially opened decoder with its manifest representation.
	OPENVOLUMETRIC_API int openvolumetric_configure_adaptive_instance(
		int id,
		const char* representation_id);
	/// Warms a representation and commits it at the requested media boundary.
	OPENVOLUMETRIC_API int openvolumetric_request_adaptive_switch(
		int id,
		const char* resource,
		const char* representation_id,
		double boundary_time,
		const char* reason);
	OPENVOLUMETRIC_API void openvolumetric_cancel_adaptive_switch(int id);
	/// Snapshots switch state, generation, count, and boundary for later getters.
	OPENVOLUMETRIC_API int openvolumetric_get_adaptive_switch_details(
		int id,
		int& state,
		unsigned long long& generation,
		unsigned long long& switch_count,
		double& boundary_time);
	OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_switch_active_id();
	OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_switch_pending_id();
	OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_switch_reason();
	/// Returns the most recent manifest-selection error.
	OPENVOLUMETRIC_API const char* openvolumetric_get_adaptive_error();

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
