#pragma once

#include <cstdarg>
#include <mutex>

namespace openvolumetric
{

/** Stable severity values passed to engine and test log sinks. */
enum class LogLevel : int
{
	Debug = 0,
	Info = 1,
	Warning = 2,
	Error = 3
};

/**
 * Host log callback.
 *
 * The message is borrowed and valid only for the callback. The callback may
 * run on decoder, transport, or render threads and must not retain the pointer.
 */
using LogCallback = void (*)(LogLevel level, const char* message, void* user);

/** Thread-safe native diagnostic router with a platform fallback sink. */
class Logger final
{
public:
	/** Returns the function-local process service; no manual lifetime exists. */
	static Logger& instance();

	/** Atomically replaces the optional host callback and its opaque context. */
	void set_callback(LogCallback callback, void* user);
	/** Removes the callback only when both values still identify its owner. */
	void clear_callback(LogCallback callback, void* user);

	/** Formats into bounded stack storage, then publishes one complete message. */
	void write(LogLevel level, const char* format, ...);
	void write_v(LogLevel level, const char* format, std::va_list arguments);

private:
	Logger() = default;
	void fallback(LogLevel level, const char* message);

	std::mutex m_mutex;
	LogCallback m_callback = nullptr;
	void* m_user = nullptr;
};

} // namespace openvolumetric

#if !defined(OPENVOLUMETRIC_DISABLE_LOGGING)
#define LOG(...) \
	::openvolumetric::Logger::instance().write( \
		::openvolumetric::LogLevel::Info, __VA_ARGS__)
#define LOG_WARNING(...) \
	::openvolumetric::Logger::instance().write( \
		::openvolumetric::LogLevel::Warning, __VA_ARGS__)
#define LOG_ERROR(...) \
	::openvolumetric::Logger::instance().write( \
		::openvolumetric::LogLevel::Error, __VA_ARGS__)
#define LOG_DEBUG(...) \
	::openvolumetric::Logger::instance().write( \
		::openvolumetric::LogLevel::Debug, __VA_ARGS__)
#else
#define LOG(...)
#define LOG_WARNING(...)
#define LOG_ERROR(...)
#define LOG_DEBUG(...)
#endif
