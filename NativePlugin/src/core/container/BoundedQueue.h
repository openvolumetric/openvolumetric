#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

namespace volumetric_video
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
	explicit BoundedQueue(std::size_t capacity)
		: m_capacity(capacity), m_state(QueueState::Open)
	{
	}

	bool try_push(T value)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_state != QueueState::Open || m_items.size() >= m_capacity)
			return false;
		m_items.push_back(std::move(value));
		return true;
	}

	template<typename Function>
	decltype(auto) access(Function function)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return function(m_items);
	}

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

	void clear()
	{
		clear([](T&) {});
	}

	bool full() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_items.size() >= m_capacity;
	}

	std::size_t size() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_items.size();
	}

	void mark_end_of_stream()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_state != QueueState::Error)
			m_state = QueueState::EndOfStream;
	}

	void set_error(std::string message)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_state = QueueState::Error;
		m_error = std::move(message);
	}

	QueueState state() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_state;
	}

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

} // namespace volumetric_video
