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

/// Minimal process-wide logger shared by the native core and engine adapters.
///
/// Android messages are forwarded to logcat. Desktop builds can optionally
/// attach a console for diagnostics requested by the managed wrapper.
class Logger
{
public:
	/// Returns the lazily constructed process-wide logger.
	static Logger* instance();

	/// Opens or attaches the platform's optional diagnostic console.
	void open_external_console();

	/// Closes a console previously opened by open_external_console().
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

	/// Prevents duplicate console allocation and release.
	bool console_active;
};
