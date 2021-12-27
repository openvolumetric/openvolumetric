#pragma once

extern "C" 
{
	//-----------------------------------------------
	// General Functions
	//-----------------------------------------------

	// Open external console to see c++ debug  info
	__declspec(dllexport) void	volumetricvideo_open_external_console();

	// Close external console to see c++ debug  info
	__declspec(dllexport) void	volumetricvideo_close_external_console();

	// Init plugin
	__declspec(dllexport) int	volumetricvideo_init(int& ID);
	
	// Quit application
	__declspec(dllexport) void	volumetricvideo_quit(int ID);


	//-----------------------------------------------
	// Decoding Functions
	//-----------------------------------------------

	// Start decoding
	__declspec(dllexport) int	volumetricvideo_start_decoding(int ID);

	// Stop Decoding
	__declspec(dllexport) int	volumetricvideo_stop_decoding(int ID);

	// Frame Index to display
	__declspec(dllexport) int	volumetricvideo_update(int ID);

	// Frame Index to display
	__declspec(dllexport) int	volumetricvideo_seek(int ID, double time);


	//-----------------------------------------------
	// Video Functions
	//-----------------------------------------------

	// Load video function
	__declspec(dllexport) int	volumetricvideo_load_video(int ID, const char* filepath);

	// Get Video Details
	__declspec(dllexport) int	volumetricvideo_get_video_details(int ID, int& width, int& height, double& fps, double& duration);

	// Set texture Pointer
	__declspec(dllexport) int	volumetricvideo_get_texture_pointers(int ID, void*& yPointer, void*& uPointer, void*& vPointer);


	//-----------------------------------------------
	// Geometry Functions
	//-----------------------------------------------
 
	// Load mesh Function
	__declspec(dllexport) int	volumetricvideo_load_mesh(int ID, const char* filename, int start_frame, int end_frame);

	// Set mesh pointers
	__declspec(dllexport) int	volumetricvideo_set_mesh_pointer(int ID);



}
