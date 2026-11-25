#include "NetworkClient.h"
#include <WS2tcpip.h>
#include <mstcpip.h>	// tcp_keepalive, SIO_KEEPALIVE_VALS
#include <cstring>
#include "Protocol.h"

constexpr ULONG KEEPALIVE_IDLE_MS = 5000;		// 이만큼 조용하면 확인 패킷을 보낸다 (기본값은 2시간)
constexpr ULONG KEEPALIVE_INTERVAL_MS = 1000;	// 응답이 없으면 이 간격으로 다시 보낸다 (Windows는 10번 재전송 후 끊는다)

bool NetworkClient::Connect(const char* serverIp, HWND wnd) {
	hwnd = wnd;
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET) return false;

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_TCP_PORT);
	if (inet_pton(AF_INET, serverIp, &addr.sin_addr) != 1) {	
		Close();
		return false;
	}
	if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
		Close();
		return false;
	}

	BOOL noDelay = TRUE;
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));

	// 서버가 신호 없이 사라져도(랜선 분리, 전원 꺼짐) 알아내도록 keepalive를 켠다. 끊기면 수신 스레드의 recv가 오류로 끝난다
	BOOL keepAlive = TRUE;
	setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&keepAlive, sizeof(keepAlive));
	tcp_keepalive ka{ 1, KEEPALIVE_IDLE_MS, KEEPALIVE_INTERVAL_MS };	// 켜기, 조용한 시간, 재전송 간격
	DWORD kaBytes = 0;
	WSAIoctl(sock, SIO_KEEPALIVE_VALS, &ka, sizeof(ka), nullptr, 0, &kaBytes, nullptr, nullptr);

	recvThread = CreateThread(nullptr, 0, RecvThread, this, 0, nullptr);
	if (recvThread == nullptr) {
		Close();
		return false;
	}
	return true;
}

void NetworkClient::Close() {
	if (sock != INVALID_SOCKET) {
		closesocket(sock);	// 수신 스레드의 블로킹 recv가 오류로 깨어나 스레드가 끝난다
		sock = INVALID_SOCKET;
	}
	if (recvThread) {
		WaitForSingleObject(recvThread, INFINITE);
		CloseHandle(recvThread);
		recvThread = nullptr;
	}
	recvBuf.clear();
}

void NetworkClient::SendLogin(const char* nickname) {
	C2S_Login pkt{};
	InitPacket(pkt, C2S_LOGIN);
	strncpy_s(pkt.nickname, nickname, _TRUNCATE);	
	Send(&pkt, sizeof(pkt));
}

void NetworkClient::SendMapSelect(uint8_t mapId) {
	C2S_MapSelect pkt{};
	InitPacket(pkt, C2S_MAP_SELECT);
	pkt.mapId = mapId;
	Send(&pkt, sizeof(pkt));
}

void NetworkClient::SendSelectWeapon(uint8_t cardIndex) {
	C2S_SelectWeapon pkt{};
	InitPacket(pkt, C2S_SELECT_WEAPON);
	pkt.cardIndex = cardIndex;
	Send(&pkt, sizeof(pkt));
}

DWORD WINAPI NetworkClient::RecvThread(LPVOID self) {
	NetworkClient* nc = static_cast<NetworkClient*>(self);
	char buf[4096];
	while (true) {
		int len = recv(nc->sock, buf, sizeof(buf), 0);	// 데이터가 올 때까지 이 스레드만 멈춘다
		if (len <= 0) break;				
		if (!nc->OnRecv(buf, len)) break;	// 오류 처리
	}
	PostMessage(nc->hwnd, WM_NET_DISCONNECTED, 0, 0);
	return 0;
}

bool NetworkClient::OnRecv(const char* data, int len) {
	recvBuf.insert(recvBuf.end(), data, data + len);

	size_t offset = 0;
	while (recvBuf.size() - offset >= sizeof(PacketHeader)) {
		const PacketHeader* h = reinterpret_cast<const PacketHeader*>(recvBuf.data() + offset);
		if (h->size < sizeof(PacketHeader) || h->size > MAX_PACKET_SIZE)
			return false;
		if (recvBuf.size() - offset < h->size)
			break;
		HandlePacket(h);
		offset += h->size;
	}
	recvBuf.erase(recvBuf.begin(), recvBuf.begin() + offset);
	return true;
}

bool NetworkClient::Send(const void* data, int len) {
	const char* p = static_cast<const char*>(data);
	while (len > 0) {		// 블로킹 send도 일부만 보낼 수 있어 반복한다
		int sent = send(sock, p, len, 0);
		if (sent == SOCKET_ERROR) return false;
		p += sent;
		len -= sent;
	}
	return true;
}

void NetworkClient::HandlePacket(const PacketHeader* pkt) {
	switch (pkt->type) {
	case S2C_LOGIN_OK:
		if (pkt->size == sizeof(S2C_LoginOk)) {
			const S2C_LoginOk* p = reinterpret_cast<const S2C_LoginOk*>(pkt);
			PostMessage(hwnd, WM_NET_LOGIN_OK, p->playerId, (LPARAM)p->token);
		}
		break;
	case S2C_UDP_OK:
		PostMessage(hwnd, WM_NET_UDP_OK, 0, 0);
		break;
	case S2C_GAME_START:
		if (pkt->size == sizeof(S2C_GameStart)) {
			const S2C_GameStart* p = reinterpret_cast<const S2C_GameStart*>(pkt);
			PostMessage(hwnd, WM_NET_GAME_START, p->mapId, 0);
		}
		break;
	case S2C_LEVEL_UP:
		if (pkt->size == sizeof(S2C_LevelUp)) {
			const S2C_LevelUp* p = reinterpret_cast<const S2C_LevelUp*>(pkt);
			WPARAM choices = p->choices[0] | (p->choices[1] << 8) | (p->choices[2] << 16);
			PostMessage(hwnd, WM_NET_LEVEL_UP, choices, 0);
		}
		break;
	}

}