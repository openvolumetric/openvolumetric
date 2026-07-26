#pragma once

#include <IVolumetricVideo.h>

class VolumetricVideoVulkan : public IVolumetricVideo
{
public:
    explicit VolumetricVideoVulkan(int id);
    ~VolumetricVideoVulkan() override;

    void destroy() override;
    int start() override;
    int stop() override;
    int render() override;
    int seek(double time) override;
};
