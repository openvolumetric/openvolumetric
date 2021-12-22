#pragma once

extern "C"
{
	// Init
	__declspec(dllexport) int	volumetricvideo_init(int& ID);
	
	// Quit application
	__declspec(dllexport) void	volumetricvideo_quit(int ID);

	// Open external console to see c++ debug  info
	__declspec(dllexport) void	volumetricvideo_open_external_console();

	// Close external console to see c++ debug  info
	__declspec(dllexport) void	volumetricvideo_close_external_console();





	//TO IMPLEMENT


	// Frame Index to display
	__declspec(dllexport) int	volumetricvideo_set_frame(int ID, int frame_index);
  
	// Load video function
	__declspec(dllexport) int	volumetricvideo_load_video(int ID, const char* filename, int& fps, int& width, int& height);

	// Set texture Pointer
	__declspec(dllexport) int	volumetricvideo_set_texture_pointer(int ID, void*& yPointer, void*& uPointer, void*& vPointer);



	// Load mesh Function
	__declspec(dllexport) int	volumetricvideo_load_mesh(int ID, const char* filename, int start_frame, int end_frame);

	// Set mesh pointers
	__declspec(dllexport) int	volumetricvideo_set_mesh_pointer(int ID);

}
