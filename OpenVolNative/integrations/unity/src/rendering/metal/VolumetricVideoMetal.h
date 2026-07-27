#pragma once

#include <IVolumetricVideo.h>

namespace openvol::unity
{

/// Metal-specific OpenVol coordinator used by the Unity native API.
class VolumetricVideoMetal : public IVolumetricVideo
{
public:
	/// Creates the FFmpeg, Draco, Metal texture, and mesh upload components.
	explicit VolumetricVideoMetal(int ID);
	/// Releases every owned component.
	~VolumetricVideoMetal() override;

	/// Starts media and geometry decode workers.
	int start() override;
	/// Stops both decode workers.
	int stop() override;
	/// Performs a unified seek and generation reset.
	int seek(double time) override;
	/// Matches and uploads one timestamped texture/mesh presentation.
	int render() override;
	/// Idempotently releases all owned components.
	void destroy() override;
};

} // namespace openvol::unity
