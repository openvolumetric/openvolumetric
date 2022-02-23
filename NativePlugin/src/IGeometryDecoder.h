#pragma once

#include <IDecoder.h>

#include <Mesh.h>


class IGeometryDecoder : public IDecoder
{

public:

	// --------------------------------------------------------------------------
	// Constructor
	// --------------------------------------------------------------------------
	IGeometryDecoder():m_current_frame(0){};


	// --------------------------------------------------------------------------
	// destructor
	// --------------------------------------------------------------------------
	~IGeometryDecoder() {};

	// --------------------------------------------------------------------------
	// Set current frame to decode from
	// --------------------------------------------------------------------------
	void set_current_frame(int current_frame) { this->m_current_frame = current_frame; }


	// --------------------------------------------------------------------------
	// function to init decoder with frame-by-frame loading
	// --------------------------------------------------------------------------
	virtual bool init(char* filepattern, int start_index, int stop_index) = 0;

	// --------------------------------------------------------------------------
	// Get mesh data for a given frame 
	// --------------------------------------------------------------------------
	virtual bool get_mesh_data(int frame_index, Mesh& mesh) = 0;


	// --------------------------------------------------------------------------
	// function to clear 
	// --------------------------------------------------------------------------
	virtual void clear_frame_data() = 0;
	

	virtual void destroy() = 0;



protected:

	// --------------------------------------------------------------------------
	// current frame being decoded
	// --------------------------------------------------------------------------
	int m_current_frame;


};

