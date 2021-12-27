#pragma once
// --------------------------------------------------------------------------
// ENUM for buffer state
// --------------------------------------------------------------------------
enum BufferState { EMPTY, NORMAL, FULL };

// --------------------------------------------------------------------------
// stream infor struct
// --------------------------------------------------------------------------
struct StreamInfo
{
	StreamInfo() : is_enabled(false), last_time(0.0), total_time(0.0), buffer_state(EMPTY) {}

	bool is_enabled;
	double last_time;
	double total_time;
	BufferState buffer_state;
};

