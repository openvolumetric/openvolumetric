#pragma once
/// Basic metadata shared by decoded media streams.
struct StreamInfo
{
	/// Constructs a disabled stream with no observed timestamps.
	StreamInfo() : is_enabled(false), last_time(0.0), total_time(0.0) {}

	/// Whether the corresponding stream exists and initialized successfully.
	bool is_enabled;
	/// Most recent decoded presentation timestamp, in seconds.
	double last_time;
	/// Container-reported stream duration, in seconds.
	double total_time;
};
