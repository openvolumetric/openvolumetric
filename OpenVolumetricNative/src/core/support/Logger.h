#pragma once
#include <stdio.h>
#include <stdarg.h>

#define ENABLE_LOG
#ifdef ENABLE_LOG
#define LOG(...) Logger::instance()->log(__VA_ARGS__)
#else
#define LOG(...)
#endif

#include <string>

namespace openvolumetric
{

/// Minimal process-wide logger shared by the native core and engine adapters.
///
/// Android messages are forwarded to logcat. Desktop builds can optionally
/// attach a console for diagnostics requested by a host.
class Logger
{
public:
	/// Returns the lazily constructed process-wide logger.
	static Logger* instance();
	void open_external_console();
	void close_external_console();

	/// Writes one printf-style diagnostic message to the platform sink.
	void log(const char* str, ...);
	   
private:
	/// Constructs the default platform logger.
	Logger();

	/// Constructs a desktop logger that appends stdout to filename.
	Logger(std::string filename);
	
	/// Backing pointer for instance().
	static Logger* _instance;
	bool console_active;
};

} // namespace openvolumetric
