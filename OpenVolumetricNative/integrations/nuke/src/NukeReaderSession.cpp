#include "NukeReaderSession.h"

#include <filesystem>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace openvolumetric::nuke
{
namespace
{
std::mutex g_sessions_mutex;
std::unordered_map<std::string, std::weak_ptr<NukeReaderSession>> g_sessions;

std::string canonical_source(const std::string& source)
{
	if (source.find("://") != std::string::npos)
		return source;
	std::error_code error;
	const std::filesystem::path absolute =
		std::filesystem::absolute(std::filesystem::path(source), error);
	if (error)
		return std::filesystem::path(source).lexically_normal().string();
	return absolute.lexically_normal().string();
}

std::string session_key(
	const std::string& source,
	std::size_t maximum_cached_presentations,
	std::size_t maximum_cache_megabytes,
	int reload_version)
{
	return canonical_source(source) + "\n" +
		std::to_string(maximum_cached_presentations) + ":" +
		std::to_string(maximum_cache_megabytes) + ":" +
		std::to_string(reload_version);
}

void prune_expired_sessions()
{
	for (auto candidate = g_sessions.begin(); candidate != g_sessions.end();)
	{
		if (candidate->second.expired())
			candidate = g_sessions.erase(candidate);
		else
			++candidate;
	}
}
} // namespace

NukeReaderSession::NukeReaderSession(
	std::string source,
	OpenVolumetricFrameReaderOptions options)
	: m_source(std::move(source)), m_reader(options)
{
}

bool NukeReaderSession::open()
{
	return m_reader.open(m_source.c_str());
}

std::shared_ptr<NukeReaderSession> NukeReaderSession::acquire(
	const std::string& source,
	std::size_t maximum_cached_presentations,
	std::size_t maximum_cache_megabytes,
	int reload_version,
	std::string& error)
{
	if (source.empty())
	{
		error = "Select an OpenVolumetric MP4 file.";
		return {};
	}

	const std::string canonical = canonical_source(source);
	const std::string key = session_key(
		source,
		maximum_cached_presentations,
		maximum_cache_megabytes,
		reload_version);
	std::lock_guard<std::mutex> lock(g_sessions_mutex);
	prune_expired_sessions();
	auto existing = g_sessions.find(key);
	if (existing != g_sessions.end())
	{
		if (auto shared = existing->second.lock())
			return shared;
	}

	OpenVolumetricFrameReaderOptions options;
	options.maximum_cached_presentations = maximum_cached_presentations;
	constexpr std::size_t bytes_per_megabyte = 1024U * 1024U;
	options.maximum_cache_bytes = maximum_cache_megabytes >
			std::numeric_limits<std::size_t>::max() / bytes_per_megabyte
		? std::numeric_limits<std::size_t>::max()
		: maximum_cache_megabytes * bytes_per_megabyte;
	auto session = std::shared_ptr<NukeReaderSession>(
		new NukeReaderSession(canonical, options));
	if (!session->open())
	{
		error = session->error();
		return {};
	}
	g_sessions[key] = session;
	return session;
}

OpenVolumetricMediaInfo NukeReaderSession::media_info() const
{
	return m_reader.media_info();
}

FrameRequestResult NukeReaderSession::request(
	double presentation_time,
	std::shared_ptr<const OpenVolumetricPresentation>& output)
{
	return m_reader.request(presentation_time, output);
}

std::string NukeReaderSession::error() const
{
	return m_reader.error();
}

const char* request_result_message(FrameRequestResult result)
{
	switch (result)
	{
	case FrameRequestResult::InvalidTime:
		return "The requested timeline frame maps before the media start.";
	case FrameRequestResult::NotOpen:
		return "The OpenVolumetric source is not open.";
	case FrameRequestResult::Missing:
		return "The requested time has no complete texture and geometry sample.";
	case FrameRequestResult::Timeout:
		return "Timed out decoding the requested OpenVolumetric presentation.";
	case FrameRequestResult::Cancelled:
		return "The OpenVolumetric frame request was superseded.";
	case FrameRequestResult::Failed:
		return "OpenVolumetric failed to decode the requested presentation.";
	case FrameRequestResult::Ready:
		break;
	}
	return "";
}

} // namespace openvolumetric::nuke
