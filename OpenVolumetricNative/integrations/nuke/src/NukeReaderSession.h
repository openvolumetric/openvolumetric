#pragma once

#include <OpenVolumetricFrameReader.h>

#include <memory>
#include <string>

namespace openvolumetric::nuke
{

/// Shared demand-driven decoder for Nuke nodes reading the same source.
class NukeReaderSession final
{
public:
	static std::shared_ptr<NukeReaderSession> acquire(
		const std::string& source,
		std::size_t maximum_cached_presentations,
		std::size_t maximum_cache_megabytes,
		int reload_version,
		std::string& error);

	OpenVolumetricMediaInfo media_info() const;
	FrameRequestResult request(
		double presentation_time,
		std::shared_ptr<const OpenVolumetricPresentation>& output);
	std::string error() const;

private:
	NukeReaderSession(
		std::string source,
		OpenVolumetricFrameReaderOptions options);
	bool open();

	std::string m_source;
	OpenVolumetricFrameReader m_reader;
};

/// Returns a stable user-facing summary for a frame-reader result.
const char* request_result_message(FrameRequestResult result);

} // namespace openvolumetric::nuke
