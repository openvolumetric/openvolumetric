#pragma once

#include <Unity/IUnityInterface.h>

#define OPENVOLUMETRIC_API UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API

// C ABI consumed by VolumetricVideoDecoder.cs. Decoder instances are
// identified by the integer returned from openvolumetric_init().
//
// Resource setup runs on Unity's main thread. Frame uploads happen through
// GetRenderEventFunc() on Unity's render thread, using the texture and mesh
// resources registered through this interface.
extern "C" 
{
	//-----------------------------------------------
	// General Functions
	//-----------------------------------------------

	/// Opens the optional desktop console used for native diagnostics.
	OPENVOLUMETRIC_API void	openvolumetric_open_external_console();

	/// Closes a diagnostic console previously opened by this plugin.
	OPENVOLUMETRIC_API void	openvolumetric_close_external_console();

	/// Creates a graphics-backend instance and writes its stable identifier.
	/// Returns 1 on success and -1 when the active graphics API is unsupported.
	OPENVOLUMETRIC_API int	openvolumetric_init(int& ID);
	
	/// Destroys one instance and releases all of its native resources.
	OPENVOLUMETRIC_API void	openvolumetric_quit(int ID);

	/// Sets the desired presentation time from the engine playback clock.
	OPENVOLUMETRIC_API void openvolumetric_set_time(int ID, double time);


	//-----------------------------------------------
	// Decoding Functions
	//-----------------------------------------------

	/// Starts media and geometry worker threads for one initialized instance.
	OPENVOLUMETRIC_API int	openvolumetric_start_decoding(int ID);

	/// Stops worker threads while preserving initialized graphics resources.
	OPENVOLUMETRIC_API int	openvolumetric_stop_decoding(int ID);

	/// Seeks the unified media pipeline to time in seconds.
	OPENVOLUMETRIC_API int	openvolumetric_seek(int ID, double time);


	//-----------------------------------------------
	// Video Functions
	//-----------------------------------------------

	/// Opens a combined MP4. Fails if video or vvge geometry is absent.
	OPENVOLUMETRIC_API int	openvolumetric_load_video(int ID, const char* filepath);

	/// Returns a borrowed human-readable error string for the instance.
	OPENVOLUMETRIC_API const char* openvolumetric_get_last_error(int ID);
	/// Returns the timestamp most recently uploaded by the render thread.
	OPENVOLUMETRIC_API double openvolumetric_get_last_presented_time(int ID);

	/// Retrieves decoded dimensions, nominal FPS, and duration in seconds.
	OPENVOLUMETRIC_API int	openvolumetric_get_video_details(int ID, int& width, int& height, double& fps, double& duration);

	/// Retrieves decoded PCM layout. Returns 0 when audio is absent.
	OPENVOLUMETRIC_API int	openvolumetric_get_audio_details(int ID, int& sample_rate, int& channels);

	/// Pulls interleaved float PCM, filling unavailable samples with silence.
	OPENVOLUMETRIC_API int	openvolumetric_read_audio(int ID, float* samples, int sample_count);

	/// Returns platform texture handles used to construct Unity textures.
	OPENVOLUMETRIC_API int	openvolumetric_get_texture_pointers(int ID, void*& yPointer, void*& uPointer, void*& vPointer);
	/// Registers the handles Unity exposes after creating external textures.
	OPENVOLUMETRIC_API int openvolumetric_register_texture_pointers(
		int ID, void* yPointer, void* uPointer, void* vPointer);


	//-----------------------------------------------
	// Geometry Functions
	//-----------------------------------------------

	/// Registers Unity-owned index and vertex buffers for native uploads.
	OPENVOLUMETRIC_API int	openvolumetric_set_mesh_pointer(int ID, void* indexBufferHandle, int index_size, void* vertexBufferHandle, int vertex_size);

}
