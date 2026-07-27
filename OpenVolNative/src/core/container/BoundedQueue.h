#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

namespace openvol
{

enum class QueueState
{
	Open,
	EndOfStream,
	Error
};

/// Bounded queue shared between producer and consumer threads.
///
/// access() exists for frame-index matching that must inspect and discard
/// several queued items atomically. Callers must not retain deque references.
template<typename T>
class BoundedQueue
{
public:
	/// Creates an empty queue that rejects pushes after capacity items.
	explicit BoundedQueue(std::size_t capacity)
		: m_capacity(capacity), m_state(QueueState::Open)
	{
	}

	/// Moves value into the queue when it is open and has capacity.
	///
	/// This method never blocks; callers implement backpressure by retrying.
	bool try_push(T value)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_state != QueueState::Open || m_items.size() >= m_capacity)
			return false;
		m_items.push_back(std::move(value));
		return true;
	}

	/// Runs function while holding the queue lock.
	///
	/// Use this for atomic timestamp matching that may inspect and remove
	/// several elements. References must not escape the callback.
	template<typename Function>
	decltype(auto) access(Function function)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return function(m_items);
	}

	/// Removes every item, invoking cleanup before resetting terminal state.
	template<typename Cleanup>
	void clear(Cleanup cleanup)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (T& item : m_items)
			cleanup(item);
		m_items.clear();
		m_state = QueueState::Open;
		m_error.clear();
	}

	/// Removes every item and returns the queue to its open state.
	void clear()
	{
		clear([](T&) {});
	}

	/// Returns whether another push would exceed the configured capacity.
	bool full() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_items.size() >= m_capacity;
	}

	/// Returns a thread-safe snapshot of the queued item count.
	std::size_t size() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_items.size();
	}

	/// Prevents further pushes and tells consumers no more items will arrive.
	void mark_end_of_stream()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_state != QueueState::Error)
			m_state = QueueState::EndOfStream;
	}

	/// Stores a terminal error and prevents further pushes.
	void set_error(std::string message)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_state = QueueState::Error;
		m_error = std::move(message);
	}

	/// Returns a thread-safe snapshot of the terminal/open state.
	QueueState state() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_state;
	}

	/// Returns the terminal error text, or an empty string when none is set.
	std::string error() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_error;
	}

private:
	const std::size_t m_capacity;
	mutable std::mutex m_mutex;
	std::deque<T> m_items;
	QueueState m_state;
	std::string m_error;
};

} // namespace openvol
