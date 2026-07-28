#pragma once

#include <vector>

namespace openvolumetric
{

/// Interleaved vertex layout shared by the core and engine upload backends.
///
/// Field ordering must remain synchronized with Unity's Mesh vertex attribute
/// configuration and any future Unreal vertex declaration.
struct Vertex
{
	public:


		/// Constructs a zero-filled vertex.
		Vertex()
		{
			pos[0] = 0.0;			pos[1] = 0.0;			pos[2] = 0.0;
			normal[0] = 0.0;		normal[1] = 0.0;		normal[2] = 0.0;
			uv[0] = 0.0;			uv[1] = 0.0;
		}

		/// Constructs a complete position, normal, and UV vertex.
		Vertex(float x, float y, float z, float nx, float ny, float nz, float u, float v)
		{
			pos[0] = x;			pos[1] = y;			pos[2] = z;
			normal[0] = nx;		normal[1] = ny;		normal[2] = nz;
			uv[0] = u;			uv[1] = v;

		}

		/// Object-space position.
		float pos[3];

		/// Object-space unit normal.
		float normal[3];

		/// Primary texture coordinates.
		float uv[2];
};

/// Engine-neutral triangle mesh produced by the geometry decoder.
class Mesh
{
public:
	/// Constructs an empty mesh.
	Mesh() {};

	/// Populates a unit quad used for graphics-path diagnostics.
	bool init_quad()
	{

		// The order matches the Vertex layout expected by every uploader.
		verts.push_back(Vertex(0, 0, 0, 0, 0, -1, 0, 0));
		verts.push_back(Vertex(1, 0, 0, 0, 0, -1, 1, 0));
		verts.push_back(Vertex(0, 1, 0, 0, 0, -1, 0, 1));
		verts.push_back(Vertex(1, 1, 0, 0, 0, -1, 1, 1));

		// Two clockwise triangles cover the four vertices.
		indexes.push_back(0);
		indexes.push_back(2);
		indexes.push_back(1);
		indexes.push_back(2);
		indexes.push_back(3);
		indexes.push_back(1);
		return true;
	}

	/// Interleaved vertex payload.
	std::vector<Vertex> verts;

	/// Triangle-list indices into verts.
	std::vector<int> indexes;

};

} // namespace openvolumetric
