#pragma once
#include <Windows.h>
#include <vector>

template<class T>
class EventQueue {
public:
	EventQueue() { InitializeCriticalSection(&cs); }
	~EventQueue() { DeleteCriticalSection(&cs); }
	EventQueue(const EventQueue&) = delete;
	EventQueue& operator=(const EventQueue&) = delete;

	void Push(T e) {
		EnterCriticalSection(&cs);
		items.push_back(std::move(e));
		LeaveCriticalSection(&cs);
	}

	void SwapOut(std::vector<T>& out) {
		out.clear();
		EnterCriticalSection(&cs);
		items.swap(out);
		LeaveCriticalSection(&cs);
	}

private:
	CRITICAL_SECTION cs;
	std::vector<T> items;
};
