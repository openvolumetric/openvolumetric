#pragma once

#include <vector>

// ------------------------------------------------
// Vertex Data Structure - this maps to unity internal buffer representation
//
struct Vertex
{
	public:


		// ------------------------------------------------
		// 
		//
		Vertex()
		{
			pos[0] = 0.0;			pos[1] = 0.0;			pos[2] = 0.0;
			normal[0] = 0.0;		normal[1] = 0.0;		normal[2] = 0.0;
			uv[0] = 0.0;			uv[1] = 0.0;
		}

		// ------------------------------------------------
		// 
		//
		Vertex(float x, float y, float z, float nx, float ny, float nz, float u, float v)
		{
			pos[0] = x;			pos[1] = y;			pos[2] = z;
			normal[0] = nx;		normal[1] = ny;		normal[2] = nz;
			uv[0] = u;			uv[1] = v;

		}

		// ------------------------------------------------
		// 
		//
		float pos[3];

		// ------------------------------------------------
		// 
		//

		float normal[3];

		// ------------------------------------------------
		// 
		//
		float uv[2];
};



// ------------------------------------------------
// Mesh Data Structure
//
class Mesh
{
public:

	// ------------------------------------------------
	// 
	//
	Mesh() {};

	// ------------------------------------------------
	// 
	//
	bool init_quad()
	{

		//
		verts.push_back(Vertex(0, 0, 0, 0, 0, -1, 0, 0));
		verts.push_back(Vertex(1, 0, 0, 0, 0, -1, 1, 0));
		verts.push_back(Vertex(0, 1, 0, 0, 0, -1, 0, 1));
		verts.push_back(Vertex(1, 1, 0, 0, 0, -1, 1, 1));

		//
		indexes.push_back(0);
		indexes.push_back(2);
		indexes.push_back(1);
		indexes.push_back(2);
		indexes.push_back(3);
		indexes.push_back(1);
		return true;
	}

	// ------------------------------------------------
	// 
	//
	std::vector<Vertex> verts;

	// ------------------------------------------------
	// 
	//
	std::vector<int> indexes;

};

