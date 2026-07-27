#pragma once

#include <IVolumetricVideo.h>
#include <Mesh.h>

#include <thread>

namespace openvol::unity
{

/// D3D11-specific OpenVol coordinator used by Unity on Windows.
class VolumetricVideoD3D11 : public IVolumetricVideo
{
public:
	/// Constructs an unattached coordinator.
	VolumetricVideoD3D11() : IVolumetricVideo() {};

	/// Creates media, geometry, texture, and mesh components.
	VolumetricVideoD3D11(int ID);

	/// Releases every owned component.
	~VolumetricVideoD3D11() override;

	/// Starts media and geometry workers.
	int start() override;

	/// Stops both decode workers.
	int stop() override;

	/// Performs a unified timestamp seek and generation reset.
	int seek(double time) override;

	/// Matches and uploads one timestamped texture/mesh presentation.
	int render() override;

	/// Idempotently stops workers and releases all owned components.
	void destroy() override;


private:
	/// Temporary destination used when retrieving a decoded mesh.
	Mesh m_mesh;
};

} // namespace openvol::unity
