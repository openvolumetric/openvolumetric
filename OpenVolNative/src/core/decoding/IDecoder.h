#pragma once

#include <atomic>

/// Common lifecycle state shared by the media and geometry decoders.
///
/// State is atomic because the engine and worker threads inspect it
/// concurrently. Concrete decoders remain responsible for synchronizing
/// their own queues and codec resources.
class IDecoder
{
public:
	/// High-level worker state exposed to pipeline coordinators.
	enum DecoderState
	{
		UNINITIALIZED,
		INITIALIZED,
		DECODING,
		DECODE_EOF,
		STOP
	};

	/// Constructs an uninitialized, stopped decoder.
	IDecoder():m_initialised(false), m_decoder_state(UNINITIALIZED) {};

	/// Allows concrete decoders to release worker resources polymorphically.
	virtual ~IDecoder() {};

	/// Returns whether format-specific initialization completed successfully.
	bool is_init() const { return m_initialised; }

	/// Returns a thread-safe snapshot of the current worker state.
	DecoderState get_decoder_state() const
	{
		return m_decoder_state.load(std::memory_order_acquire);
	}

	/// Starts the implementation's decode worker.
	virtual bool start_decoding() = 0;

	/// Requests worker termination and waits until it no longer touches state.
	virtual bool stop_decoding() = 0;

protected:
	/// Set only after all resources required by start_decoding() are ready.
	bool m_initialised;

	/// Atomic lifecycle state shared with engine-side coordinators.
	std::atomic<DecoderState> m_decoder_state;
};
