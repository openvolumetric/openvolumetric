#pragma once

#include <Unity/IUnityInterface.h>

#define OPENVOL_API UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API

// C ABI consumed by VolumetricVideoDecoder.cs. Decoder instances are
// identified by the integer returned from openvol_init().
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
	OPENVOL_API void	openvol_open_external_console();

	/// Closes a diagnostic console previously opened by this plugin.
	OPENVOL_API void	openvol_close_external_console();

	/// Creates a graphics-backend instance and writes its stable identifier.
	/// Returns 1 on success and -1 when the active graphics API is unsupported.
	OPENVOL_API int	openvol_init(int& ID);
	
	/// Destroys one instance and releases all of its native resources.
	OPENVOL_API void	openvol_quit(int ID);

	/// Sets the desired presentation time from the engine playback clock.
	OPENVOL_API void openvol_set_time(int ID, double time);


	//-----------------------------------------------
	// Decoding Functions
	//-----------------------------------------------

	/// Starts media and geometry worker threads for one initialized instance.
	OPENVOL_API int	openvol_start_decoding(int ID);

	/// Stops worker threads while preserving initialized graphics resources.
	OPENVOL_API int	openvol_stop_decoding(int ID);

	/// Seeks the unified media pipeline to time in seconds.
	OPENVOL_API int	openvol_seek(int ID, double time);


	//-----------------------------------------------
	// Video Functions
	//-----------------------------------------------

	/// Opens a combined MP4. Fails if video or vvge geometry is absent.
	OPENVOL_API int	openvol_load_video(int ID, const char* filepath);

	/// Returns a borrowed human-readable error string for the instance.
	OPENVOL_API const char* openvol_get_last_error(int ID);
	/// Returns the timestamp most recently uploaded by the render thread.
	OPENVOL_API double openvol_get_last_presented_time(int ID);

	/// Retrieves decoded dimensions, nominal FPS, and duration in seconds.
	OPENVOL_API int	openvol_get_video_details(int ID, int& width, int& height, double& fps, double& duration);

	/// Retrieves decoded PCM layout. Returns 0 when audio is absent.
	OPENVOL_API int	openvol_get_audio_details(int ID, int& sample_rate, int& channels);

	/// Pulls interleaved float PCM, filling unavailable samples with silence.
	OPENVOL_API int	openvol_read_audio(int ID, float* samples, int sample_count);

	/// Returns platform texture handles used to construct Unity textures.
	OPENVOL_API int	openvol_get_texture_pointers(int ID, void*& yPointer, void*& uPointer, void*& vPointer);
	/// Registers the handles Unity exposes after creating external textures.
	OPENVOL_API int openvol_register_texture_pointers(
		int ID, void* yPointer, void* uPointer, void* vPointer);


	//-----------------------------------------------
	// Geometry Functions
	//-----------------------------------------------

	/// Registers Unity-owned index and vertex buffers for native uploads.
	OPENVOL_API int	openvol_set_mesh_pointer(int ID, void* indexBufferHandle, int index_size, void* vertexBufferHandle, int vertex_size);

}
