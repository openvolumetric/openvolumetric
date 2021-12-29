#pragma once

#include <vector>

// ------------------------------------------------
// Vertex Data Structure - this maps to unity internal buffer representation
//
struct Vertex
{
	float pos[3];
	float normal[3];
	float uv[2];
};

//
struct Triangle
{
	int index[3];
};



// ------------------------------------------------
// Mesh Data Structure
//
struct Mesh
{
	// Verts
	std::vector<Vertex> verts;

	// Tris
	std::vector<Triangle> tris;

};

