#pragma once

#include <Unity/IUnityInterface.h>

#define VOLUMETRIC_VIDEO_API UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API

// C ABI consumed by VolumetricVideoDecoder.cs. Decoder instances are
// identified by the integer returned from volumetricvideo_init().
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
	VOLUMETRIC_VIDEO_API void	volumetricvideo_open_external_console();

	// Close external console to see c++ debug  info
	VOLUMETRIC_VIDEO_API void	volumetricvideo_close_external_console();

	// Init plugin
	VOLUMETRIC_VIDEO_API int	volumetricvideo_init(int& ID);
	
	// Quit application
	VOLUMETRIC_VIDEO_API void	volumetricvideo_quit(int ID);

	// Set frame index
	VOLUMETRIC_VIDEO_API void	volumetricvideo_set_frame(int ID, int frame_index);


	//-----------------------------------------------
	// Decoding Functions
	//-----------------------------------------------

	// Start decoding
	VOLUMETRIC_VIDEO_API int	volumetricvideo_start_decoding(int ID);

	// Stop Decoding
	VOLUMETRIC_VIDEO_API int	volumetricvideo_stop_decoding(int ID);

	// Frame Index to display
	VOLUMETRIC_VIDEO_API int	volumetricvideo_seek(int ID, double time);


	//-----------------------------------------------
	// Video Functions
	//-----------------------------------------------

	// Open a combined MP4. The call fails if video or vvge geometry is absent.
	VOLUMETRIC_VIDEO_API int	volumetricvideo_load_video(int ID, const char* filepath);

	// Human-readable error from the instance's most recent media operation.
	VOLUMETRIC_VIDEO_API const char* volumetricvideo_get_last_error(int ID);

	// Get Video Details
	VOLUMETRIC_VIDEO_API int	volumetricvideo_get_video_details(int ID, int& width, int& height, double& fps, double& duration);

	// Get decoded audio output format. Returns 0 when no audio stream exists.
	VOLUMETRIC_VIDEO_API int	volumetricvideo_get_audio_details(int ID, int& sample_rate, int& channels);

	// Pull interleaved float PCM. Unavailable samples are filled with silence.
	VOLUMETRIC_VIDEO_API int	volumetricvideo_read_audio(int ID, float* samples, int sample_count);

	// Return platform texture handles used to construct Unity textures.
	VOLUMETRIC_VIDEO_API int	volumetricvideo_get_texture_pointers(int ID, void*& yPointer, void*& uPointer, void*& vPointer);


	//-----------------------------------------------
	// Geometry Functions
	//-----------------------------------------------

	// Register Unity-owned index and vertex buffers for native frame uploads.
	VOLUMETRIC_VIDEO_API int	volumetricvideo_set_mesh_pointer(int ID, void* indexBufferHandle, int index_size, void* vertexBufferHandle, int vertex_size);

}
