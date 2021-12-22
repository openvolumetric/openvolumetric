#include "VolumetricVideoD3D11.h"

#include <iostream>

#include <Logger.h>


VolumetricVideoD3D11::VolumetricVideoD3D11(int ID) : IVolumetricVideo(ID) 
{
	LOG("VolumetricVideoD3D11::VolumetricVideoD3D11 - Constructor - %d", ID);
}



//----------------------------------------------
//
int VolumetricVideoD3D11::create_resources()
{


	return -1;
}


int VolumetricVideoD3D11::set_frame(int frame_index)
{


	return -1;
}




//----------------------------------------------
//
int VolumetricVideoD3D11::render()
{

	return -1;
}
