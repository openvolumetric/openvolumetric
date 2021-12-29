#pragma once

#include <IGeometryDecoder.h>


// --------------------------------------------------------------------------
//  Geometry Decoder Class using Google Draco
// --------------------------------------------------------------------------
class GeometryDecoderDraco : public IGeometryDecoder
{


public:

	//----------------------------------
	// Constructor
	// 
	GeometryDecoderDraco();

	//----------------------------------
	// Destructor
	// 
	virtual ~GeometryDecoderDraco();

	// --------------------------------------------------------------------------
	// Stop Decoding
	// 
	bool start_decoding();

	// --------------------------------------------------------------------------
	// Stop Decoding
	//
	bool stop_decoding();

};

