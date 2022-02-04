#pragma once


#include <Mesh.h>

class IMeshBuffer
{

public:
	
	//----------------------------------
	//
	IMeshBuffer() {};


	//----------------------------------
	//
	~IMeshBuffer() {};


	//----------------------------------
	//
	virtual bool init(void* handler, void* index_handle, int index_count, void* vertex_handle, int vertex_count) = 0;


	//----------------------------------
	//
	virtual bool update(Mesh * mesh) = 0;


};

