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

	// Open external console to see c++ debug  info
	OPENVOL_API void	openvol_open_external_console();

	// Close external console to see c++ debug  info
	OPENVOL_API void	openvol_close_external_console();

	// Init plugin
	OPENVOL_API int	openvol_init(int& ID);
	
	// Quit application
	OPENVOL_API void	openvol_quit(int ID);

	// Set presentation target from the engine playback clock.
	OPENVOL_API void openvol_set_time(int ID, double time);


	//-----------------------------------------------
	// Decoding Functions
	//-----------------------------------------------

	// Start decoding
	OPENVOL_API int	openvol_start_decoding(int ID);

	// Stop Decoding
	OPENVOL_API int	openvol_stop_decoding(int ID);

	// Frame Index to display
	OPENVOL_API int	openvol_seek(int ID, double time);


	//-----------------------------------------------
	// Video Functions
	//-----------------------------------------------

	// Open a combined MP4. The call fails if video or vvge geometry is absent.
	OPENVOL_API int	openvol_load_video(int ID, const char* filepath);

	// Human-readable error from the instance's most recent media operation.
	OPENVOL_API const char* openvol_get_last_error(int ID);
	OPENVOL_API double openvol_get_last_presented_time(int ID);

	// Get Video Details
	OPENVOL_API int	openvol_get_video_details(int ID, int& width, int& height, double& fps, double& duration);

	// Get decoded audio output format. Returns 0 when no audio stream exists.
	OPENVOL_API int	openvol_get_audio_details(int ID, int& sample_rate, int& channels);

	// Pull interleaved float PCM. Unavailable samples are filled with silence.
	OPENVOL_API int	openvol_read_audio(int ID, float* samples, int sample_count);

	// Return platform texture handles used to construct Unity textures.
	OPENVOL_API int	openvol_get_texture_pointers(int ID, void*& yPointer, void*& uPointer, void*& vPointer);
	OPENVOL_API int openvol_register_texture_pointers(
		int ID, void* yPointer, void* uPointer, void* vPointer);


	//-----------------------------------------------
	// Geometry Functions
	//-----------------------------------------------

	// Register Unity-owned index and vertex buffers for native frame uploads.
	OPENVOL_API int	openvol_set_mesh_pointer(int ID, void* indexBufferHandle, int index_size, void* vertexBufferHandle, int vertex_size);

}
