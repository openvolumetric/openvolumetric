#pragma once

#include "IByteSource.h"

#include <atomic>
#include <filesystem>
#include <fstream>

namespace openvolumetric
{

/// Seekable byte source backed by one local file.
class LocalFileByteSource final : public IByteSource
{
public:
	explicit LocalFileByteSource(const std::filesystem::path& path);

	std::int64_t read(
		std::uint8_t* destination,
		std::size_t capacity) override;
	std::int64_t seek(
		std::int64_t offset,
		ByteSeekOrigin origin) override;
	std::int64_t size() const override;
	bool is_seekable() const override;
	void cancel() override;
	bool is_cancelled() const override;
	const std::string& error() const override;
	ByteSourceDiagnostics diagnostics() const override;

	/// Returns whether construction opened and measured the file.
	bool is_open() const;

private:
	std::ifstream m_stream;
	std::int64_t m_size = -1;
	std::int64_t m_position = 0;
	std::atomic<bool> m_cancelled{false};
	std::string m_error;
};

} // namespace openvolumetric
