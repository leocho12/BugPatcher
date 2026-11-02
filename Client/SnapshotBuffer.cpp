#include "SnapshotBuffer.h"

SnapshotBuffer::SnapshotBuffer() { InitializeCriticalSection(&cs); }
SnapshotBuffer::~SnapshotBuffer() { DeleteCriticalSection(&cs); }

void SnapshotBuffer::WriteFragment(const char*, int) {

}

void SnapshotBuffer::CopyTo(Snapshot& out) {
	EnterCriticalSection(&cs);
	out = current;
	LeaveCriticalSection(&cs);
}