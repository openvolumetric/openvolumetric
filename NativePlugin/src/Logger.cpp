#include "Logger.h"
#include <Windows.h>

#pragma warning(disable:4996)

//
// Instance of logger
//
Logger* Logger::_instance;


//
//
//
Logger::Logger()
{
}

//
//
//
Logger::Logger(std::string filename)
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


void Logger::open_external_console()
{
	if (!Logger::instance()->console_active)
	{
		FILE* console;
		AllocConsole();
		freopen_s(&console, "CONOUT$", "wb", stdout);
		Logger::instance()->console_active = true;

		LOG("Logger::open_external_console");
	}
}

void Logger::close_external_console()
{
	if (Logger::instance()->console_active)
	{
		LOG("Logger::close_external_console");
		FreeConsole();
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
	vprintf(str, args);
	va_end(args);
	fflush(stdout);
	printf("\n");
}