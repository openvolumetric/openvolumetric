#pragma once
// --------------------------------------------------------------------------
// Stream timing information.
// --------------------------------------------------------------------------
struct StreamInfo
{
	StreamInfo() : is_enabled(false), last_time(0.0), total_time(0.0) {}

	bool is_enabled;
	double last_time;
	double total_time;
};
