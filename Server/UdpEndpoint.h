#pragma once
#include <cstdint>
#include <winsock2.h>

struct RecvEvent;
template<class T> class EventQueue;

class UdpEndpoint {
public:
	bool Init(uint16_t port);
	SOCKET GetSocket() const;
	void OnReadable(EventQueue<RecvEvent>& recvQ);
	void SendTo(int playerId, const char* data, int len);
	bool IsRegistered(int playerId)const;
};