#pragma once

#include <IVolumetricVideo.h>


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
	~VolumetricVideoD3D11() {};

	//--------------------------------------------------------
	// inherited function to create rendering resources
	int create_resources();


	//--------------------------------------------------------
	// inherited function to set frame index
	int set_frame(int frame_index);


	//--------------------------------------------------------
	// inherited function to perform rendering
	int render();



};


