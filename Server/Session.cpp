#include "Session.h"
#include "Server.h"
#include <cstring>

bool Session::OnRecv(Server& server, const char* data, int len) {
	recvBuf.insert(recvBuf.end(), data, data + len);
	size_t offset = 0;
	while (recvBuf.size() - offset >= sizeof(PacketHeader)) {
		const PacketHeader* h = reinterpret_cast<const PacketHeader*>(recvBuf.data() + offset);
		if (h->size < sizeof(PacketHeader) || h->size > MAX_PACKET_SIZE)
			return false;		
		if (recvBuf.size() - offset < h->size)
			break;				
		server.ProcessPacket(*this, h);
		offset += h->size;
	}
	recvBuf.erase(recvBuf.begin(), recvBuf.begin() + offset);	
	return true;
	}
void Session::AppendSend(const void* data, int len) {
	const char* p = static_cast<const char*>(data);
	sendBuf.insert(sendBuf.end(), p, p + len);
}
bool Session::FlushSend() {
	while (!sendBuf.empty()) {
		int sent = send(sock, sendBuf.data(), (int)sendBuf.size(), 0);
		if (sent == SOCKET_ERROR) {
			if (WSAGetLastError() == WSAEWOULDBLOCK)
				return true;	
			return false;
		}
		sendBuf.erase(sendBuf.begin(), sendBuf.begin() + sent);	
	}
	return true;
}
void Session::Reset() {
	sock = INVALID_SOCKET;
	playerId = -1;
	token = 0;
	memset(nickname, 0, sizeof(nickname));
	loggedIn = false;
	udpOkSent = false;
	recvBuf.clear();
	sendBuf.clear();
}