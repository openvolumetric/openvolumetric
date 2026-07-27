#pragma once

#include <IVolumetricVideo.h>

namespace openvol::unity
{

/// Vulkan-specific OpenVol coordinator used by Unity on Android/Quest.
class VolumetricVideoVulkan : public IVolumetricVideo
{
public:
    /// Creates media, geometry, texture, and mesh components.
    explicit VolumetricVideoVulkan(int id);
    /// Releases every owned component.
    ~VolumetricVideoVulkan() override;

    /// Idempotently stops workers and releases owned components.
    void destroy() override;
    /// Starts media and geometry workers.
    int start() override;
    /// Stops both decode workers.
    int stop() override;
    /// Uploads a complete timestamp-matched presentation.
    int render() override;
    /// Performs a unified seek and clears a pinned pending video frame.
    int seek(double time) override;

private:
    bool m_has_pending_video = false;
    double m_pending_video_time = 0.0;
};

} // namespace openvol::unity
