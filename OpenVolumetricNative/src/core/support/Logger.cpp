#include "Logger.h"

#include <array>
#include <cstdio>

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__ANDROID__)
#include <android/log.h>
#endif

namespace openvolumetric
{
namespace
{
constexpr std::size_t kMaximumLogMessage = 2048;

#if defined(__ANDROID__)
int android_priority(LogLevel level)
{
	switch (level)
	{
	case LogLevel::Debug: return ANDROID_LOG_DEBUG;
	case LogLevel::Warning: return ANDROID_LOG_WARN;
	case LogLevel::Error: return ANDROID_LOG_ERROR;
	case LogLevel::Info:
	default: return ANDROID_LOG_INFO;
	}
}
#endif
}

Logger& Logger::instance()
{
	static Logger logger;
	return logger;
}

void Logger::set_callback(LogCallback callback, void* user)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_callback = callback;
	m_user = user;
}

void Logger::clear_callback(LogCallback callback, void* user)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_callback == callback && m_user == user)
	{
		m_callback = nullptr;
		m_user = nullptr;
	}
}

void Logger::write(LogLevel level, const char* format, ...)
{
	std::va_list arguments;
	va_start(arguments, format);
	write_v(level, format, arguments);
	va_end(arguments);
}

void Logger::write_v(
	LogLevel level,
	const char* format,
	std::va_list arguments)
{
	if (format == nullptr)
		return;
	std::array<char, kMaximumLogMessage> message{};
	std::vsnprintf(message.data(), message.size(), format, arguments);
	message.back() = '\0';

	LogCallback callback = nullptr;
	void* user = nullptr;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		callback = m_callback;
		user = m_user;
	}
	if (callback != nullptr)
		callback(level, message.data(), user);
	else
		fallback(level, message.data());
}

void Logger::fallback(LogLevel level, const char* message)
{
#if defined(__ANDROID__)
	__android_log_write(android_priority(level), "OpenVolumetric", message);
#elif defined(_WIN32)
	(void)level;
	OutputDebugStringA(message);
	OutputDebugStringA("\n");
#else
	(void)level;
	std::fprintf(stderr, "%s\n", message);
	std::fflush(stderr);
#endif
}

} // namespace openvolumetric
