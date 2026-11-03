#pragma once
#include <WinSock2.h>
#include <cstdint>
#include <vector>
#include "Protocol.h"

class Server;

class Session {
public:
	bool OnRecv(Server& server, const char* data, int len);
	// 송신 버퍼에 붙인다. 실제 send는 FlushSend
	void AppendSend(const void* data, int len);
	// 송신 버퍼를 send. 다 못 보낸 바이트는 남겨 둔다. 소켓 오류면 false
	bool FlushSend();
	bool HasPendingSend() const { return !sendBuf.empty(); }	// POLLOUT 켤지 판단
	void Reset();

	SOCKET sock = INVALID_SOCKET;
	int playerId = -1;
	uint32_t token = 0;
	char nickname[NICKNAME_LEN]{};
	bool loggedIn = false;
	bool udpOkSent = false;

private:
	std::vector<char> recvBuf;
	std::vector<char> sendBuf;
};