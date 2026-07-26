#pragma once

#include <IVolumetricVideo.h>

class VolumetricVideoMetal : public IVolumetricVideo
{
public:
	explicit VolumetricVideoMetal(int ID);
	~VolumetricVideoMetal() override;

	int start() override;
	int stop() override;
	int seek(double time) override;
	int render() override;
	void destroy() override;
};
