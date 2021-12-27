#pragma once

#include <IVolumetricVideo.h>

#include <thread>

//
//
//
class VolumetricVideoD3D11 : public IVolumetricVideo
{

public:

	//--------------------------------------------------------
	// default constructor
	VolumetricVideoD3D11() : IVolumetricVideo() {};

	
	//--------------------------------------------------------
	// constructor with instance id - id passed to base class
	VolumetricVideoD3D11(int ID);


	//--------------------------------------------------------
	// destructor
	~VolumetricVideoD3D11();


	//--------------------------------------------------------
	// inherited function to set video file
	bool set_video(const char* filepath);


	//--------------------------------------------------------
	//
	int start();


	//--------------------------------------------------------
	//
	int stop();


	//--------------------------------------------------------
	// inherited function to set frame index
	int seek(double time);

	//--------------------------------------------------------
	// inherited function to perform rendering
	int render();


private:
	
	//--------------------------------------------------------
	// Thread for video decoding
	std::thread m_video_thread;


};


