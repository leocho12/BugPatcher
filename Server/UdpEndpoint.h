#pragma once
#include <cstdint>
#include <winsock2.h>
#include "GameTypes.h"

struct RecvEvent;
struct C2S_UdpHello;
template<class T> class EventQueue;

// 서버UDP소켓 1개 메인스레드에서만 사용
class UdpEndpoint {
public:
	bool Init(uint16_t port);
	SOCKET GetSocket() const;
	void OnReadable(EventQueue<RecvEvent>& recvQ);
	void SendTo(int playerId, const char* data, int len);
	bool IsRegistered(int playerId)const;

	void SetToken(int playerId, uint32_t token);
	void Unregister(int playerId);

private:
	struct Peer {
		uint32_t token = 0;// 0이면 아직 로그인 안됨
		bool registered = false;// HELLO로 주소를 등록 했는지?
		sockaddr_in addr{};// UDP주소
	};
	void OnHello(const C2S_UdpHello& pkt, const sockaddr_in& from);
	int FindPlayer(const sockaddr_in& from)const;

	SOCKET sock = INVALID_SOCKET;
	Peer peers[MAX_PLAYERS];
};