#include "Logger.h"
#if defined(_WIN32)
#include <Windows.h>
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#if defined(_MSC_VER)
#pragma warning(disable:4996)
#endif

//
// Instance of logger
//
Logger* Logger::_instance;


//
//
//
Logger::Logger() : console_active(false)
{
}

//
//
//
Logger::Logger(std::string filename) : console_active(false)
{
	fclose(stdout);
	freopen(filename.c_str(), "a", stdout);
}


//
//
//
Logger* Logger::instance()
{
	if (!_instance) 
	{
		_instance = new Logger();
	}
	return _instance;
}

//
//
//
void Logger::open_external_console()
{
	if (!Logger::instance()->console_active)
	{
#if defined(_WIN32)
		FILE* console;
		AllocConsole();
		freopen_s(&console, "CONOUT$", "wb", stdout);
#endif
		Logger::instance()->console_active = true;

		LOG("Logger::open_external_console");
	}
}

//
//
//
void Logger::close_external_console()
{
	if (Logger::instance()->console_active)
	{
		LOG("Logger::close_external_console");
#if defined(_WIN32)
		FreeConsole();
#endif
		Logger::instance()->console_active = false;
	}
}


//
//
//
void Logger::log(const char* str, ...) 
{
	va_list args;
	va_start(args, str);
#if defined(__ANDROID__)
	__android_log_vprint(
		ANDROID_LOG_INFO,
		"OpenVol",
		str,
		args);
#else
	vprintf(str, args);
	fflush(stdout);
	printf("\n");
#endif
	va_end(args);
}
