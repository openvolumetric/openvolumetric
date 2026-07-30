#include "LocalFileByteSource.h"

#include <algorithm>
#include <limits>

namespace openvolumetric
{

LocalFileByteSource::LocalFileByteSource(const std::filesystem::path& path)
	: m_stream(path, std::ios::binary | std::ios::ate)
{
	if (!m_stream)
	{
		m_error = "Could not open byte source: " + path.string();
		return;
	}

	const std::streampos end = m_stream.tellg();
	if (end < 0)
	{
		m_error = "Could not determine byte-source size: " + path.string();
		m_stream.close();
		return;
	}
	m_size = static_cast<std::int64_t>(end);
	m_stream.seekg(0, std::ios::beg);
	if (!m_stream)
	{
		m_error = "Could not seek to the start of byte source: " +
			path.string();
		m_stream.close();
		return;
	}
}

std::int64_t LocalFileByteSource::read(
	std::uint8_t* destination,
	std::size_t capacity)
{
	if (is_cancelled())
	{
		m_error = "Byte-source read was cancelled.";
		return -1;
	}
	if (!is_open() || destination == nullptr)
	{
		m_error = "Byte-source read requires an open file and destination.";
		return -1;
	}
	if (capacity == 0 || m_position >= m_size)
		return 0;

	const std::int64_t remaining = m_size - m_position;
	const std::int64_t requested = std::min<std::int64_t>(
		remaining,
		static_cast<std::int64_t>(std::min<std::size_t>(
			capacity,
			static_cast<std::size_t>(
				std::numeric_limits<std::streamsize>::max()))));
	m_stream.read(
		reinterpret_cast<char*>(destination),
		static_cast<std::streamsize>(requested));
	const std::streamsize bytes_read = m_stream.gcount();
	if (bytes_read < 0 || (bytes_read == 0 && !m_stream.eof()))
	{
		m_error = "Local byte-source read failed.";
		return -1;
	}
	m_position += static_cast<std::int64_t>(bytes_read);
	return static_cast<std::int64_t>(bytes_read);
}

std::int64_t LocalFileByteSource::seek(
	std::int64_t offset,
	ByteSeekOrigin origin)
{
	if (is_cancelled())
	{
		m_error = "Byte-source seek was cancelled.";
		return -1;
	}
	if (!is_open())
	{
		m_error = "Cannot seek a closed byte source.";
		return -1;
	}

	std::int64_t base = 0;
	switch (origin)
	{
	case ByteSeekOrigin::Begin:
		break;
	case ByteSeekOrigin::Current:
		base = m_position;
		break;
	case ByteSeekOrigin::End:
		base = m_size;
		break;
	}
	if ((offset > 0 &&
			base > std::numeric_limits<std::int64_t>::max() - offset) ||
		(offset < 0 &&
			base < std::numeric_limits<std::int64_t>::min() - offset))
	{
		m_error = "Byte-source seek offset overflowed.";
		return -1;
	}
	const std::int64_t target = base + offset;
	if (target < 0 || target > m_size)
	{
		m_error = "Byte-source seek is outside the resource.";
		return -1;
	}

	m_stream.clear();
	m_stream.seekg(static_cast<std::streamoff>(target), std::ios::beg);
	if (!m_stream)
	{
		m_error = "Local byte-source seek failed.";
		return -1;
	}
	m_position = target;
	m_error.clear();
	return m_position;
}

std::int64_t LocalFileByteSource::size() const
{
	return m_size;
}

bool LocalFileByteSource::is_seekable() const
{
	return is_open();
}

void LocalFileByteSource::cancel()
{
	m_cancelled.store(true, std::memory_order_release);
}

bool LocalFileByteSource::is_cancelled() const
{
	return m_cancelled.load(std::memory_order_acquire);
}

const std::string& LocalFileByteSource::error() const
{
	return m_error;
}

ByteSourceDiagnostics LocalFileByteSource::diagnostics() const
{
	ByteSourceDiagnostics result;
	result.state = is_cancelled()
		? ByteSourceState::Cancelled
		: (is_open() ? ByteSourceState::Ready : ByteSourceState::Error);
	result.resource_size_bytes = m_size;
	return result;
}

bool LocalFileByteSource::is_open() const
{
	return m_stream.is_open() && m_size >= 0;
}

} // namespace openvolumetric
