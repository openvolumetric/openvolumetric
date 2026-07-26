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

class Logger 
{
public:

	//
	static Logger* instance();

	//
	void open_external_console();

	//
	void close_external_console();
	   
	//
	void log(const char* str, ...);
	   
private:

	//
	Logger();

	//
	Logger(std::string filename);
	
	//
	static Logger* _instance;

	//
	bool console_active;
};
