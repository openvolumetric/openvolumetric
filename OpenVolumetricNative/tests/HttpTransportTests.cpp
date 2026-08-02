#include <HttpRangeByteSource.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{

using namespace openvolumetric;

HttpRangeByteSourceOptions test_options()
{
	HttpRangeByteSourceOptions options;
	options.block_size = 16 * 1024;
	options.maximum_cache_bytes = 32 * 1024;
	options.sequential_read_ahead_blocks = 0;
	options.fragment_read_ahead_count = 1;
	options.connection_timeout_ms = 1000;
	options.request_timeout_ms = 3000;
	options.maximum_retry_count = 2;
	options.initial_retry_delay_ms = 10;
	options.maximum_retry_delay_ms = 20;
	return options;
}

bool expected_byte(std::uint8_t value, std::uint64_t offset)
{
	return value == static_cast<std::uint8_t>(offset % 251);
}

bool test_normal_ranges(const std::string& base_url)
{
	HttpRangeByteSource source(base_url + "/range.bin", test_options());
	if (!source.is_open() || source.size() != 128 * 1024)
		return false;

	std::vector<std::uint8_t> bytes(64);
	if (source.read(bytes.data(), bytes.size()) !=
		static_cast<std::int64_t>(bytes.size()))
	{
		return false;
	}
	for (std::size_t index = 0; index < bytes.size(); ++index)
	{
		if (!expected_byte(bytes[index], index))
			return false;
	}

	constexpr std::int64_t seek_offset = 70000;
	if (source.seek(seek_offset, ByteSeekOrigin::Begin) != seek_offset ||
		source.read(bytes.data(), bytes.size()) !=
			static_cast<std::int64_t>(bytes.size()))
	{
		return false;
	}
	for (std::size_t index = 0; index < bytes.size(); ++index)
	{
		if (!expected_byte(
			bytes[index], seek_offset + static_cast<std::int64_t>(index)))
		{
			return false;
		}
	}

	const ByteSourceDiagnostics diagnostics = source.diagnostics();
	return diagnostics.state == ByteSourceState::Ready &&
		diagnostics.remote && diagnostics.request_count >= 2 &&
		diagnostics.downloaded_bytes >= 2 * test_options().block_size;
}

bool test_retry_recovery(const std::string& base_url)
{
	HttpRangeByteSource source(base_url + "/recover.bin", test_options());
	const ByteSourceDiagnostics diagnostics = source.diagnostics();
	return source.is_open() && source.error().empty() &&
		diagnostics.state == ByteSourceState::Ready &&
		diagnostics.request_count == 3 && diagnostics.recovery_count == 1;
}

bool test_retry_exhaustion(const std::string& base_url)
{
	HttpRangeByteSource source(base_url + "/exhaust.bin", test_options());
	const ByteSourceDiagnostics diagnostics = source.diagnostics();
	return !source.is_open() && !source.error().empty() &&
		diagnostics.state == ByteSourceState::Error &&
		diagnostics.request_count == 3;
}

bool test_truncated_ranges(const std::string& base_url)
{
	HttpRangeByteSource source(base_url + "/truncated.bin", test_options());
	const ByteSourceDiagnostics diagnostics = source.diagnostics();
	return !source.is_open() &&
		source.error().find("truncated") != std::string::npos &&
		diagnostics.state == ByteSourceState::Error &&
		diagnostics.request_count == 3;
}

bool test_cancellation(const std::string& base_url)
{
	HttpRangeByteSourceOptions options = test_options();
	options.request_timeout_ms = 5000;
	HttpRangeByteSource source(base_url + "/slow.bin", options);
	if (!source.is_open() ||
		source.seek(70000, ByteSeekOrigin::Begin) != 70000)
	{
		return false;
	}

	std::vector<std::uint8_t> bytes(64);
	std::int64_t read_result = 0;
	const auto started = std::chrono::steady_clock::now();
	std::thread reader([&]()
	{
		read_result = source.read(bytes.data(), bytes.size());
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	source.cancel();
	reader.join();
	const double elapsed = std::chrono::duration<double>(
		std::chrono::steady_clock::now() - started).count();
	return read_result == -1 && source.is_cancelled() &&
		source.diagnostics().state == ByteSourceState::Cancelled &&
		elapsed < 2.0;
}

} // namespace

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cerr << "Expected the test-server base URL.\n";
		return 2;
	}
	const std::string base_url = argv[1];
	if (!test_normal_ranges(base_url))
	{
		std::cerr << "Normal HTTP range test failed.\n";
		return 1;
	}
	if (!test_retry_recovery(base_url))
	{
		std::cerr << "HTTP retry recovery test failed.\n";
		return 1;
	}
	if (!test_retry_exhaustion(base_url))
	{
		std::cerr << "HTTP retry exhaustion test failed.\n";
		return 1;
	}
	if (!test_truncated_ranges(base_url))
	{
		std::cerr << "HTTP truncated-range test failed.\n";
		return 1;
	}
	if (!test_cancellation(base_url))
	{
		std::cerr << "HTTP cancellation test failed.\n";
		return 1;
	}
	return 0;
}
