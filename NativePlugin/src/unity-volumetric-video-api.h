#pragma once

#include <Unity/IUnityInterface.h>

#define VOLUMETRIC_VIDEO_API UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API

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

	// Set unity time
	VOLUMETRIC_VIDEO_API void	volumetricvideo_set_unity_time(int ID, double unity_time);

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
	VOLUMETRIC_VIDEO_API int	volumetricvideo_update(int ID);

	// Frame Index to display
	VOLUMETRIC_VIDEO_API int	volumetricvideo_seek(int ID, double time);


	//-----------------------------------------------
	// Video Functions
	//-----------------------------------------------

	// Load video function
	VOLUMETRIC_VIDEO_API int	volumetricvideo_load_video(int ID, const char* filepath);

	// Get Video Details
	VOLUMETRIC_VIDEO_API int	volumetricvideo_get_video_details(int ID, int& width, int& height, double& fps, double& duration);

	// Set texture Pointer
	VOLUMETRIC_VIDEO_API int	volumetricvideo_get_texture_pointers(int ID, void*& yPointer, void*& uPointer, void*& vPointer);


	//-----------------------------------------------
	// Geometry Functions
	//-----------------------------------------------

	// Load mesh data Function
	VOLUMETRIC_VIDEO_API int	volumetricvideo_load_mesh_data(int ID, char* filepattern, int start_frame, int end_frame);

	// Set mesh pointers
	VOLUMETRIC_VIDEO_API int	volumetricvideo_set_mesh_pointer(int ID, void* indexBufferHandle, int index_size, void* vertexBufferHandle, int vertex_size);

}
