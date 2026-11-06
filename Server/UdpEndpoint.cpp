#include "UdpEndpoint.h"
#include <msWsock.h>// 윈속 기능 확장
#include <cstdio>
#include "EventQueue.h"
#include "Events.h"

bool UdpEndpoint::Init(uint16_t port) {
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET) {
		printf("UDP socket fail:%d\n", WSAGetLastError());
		return false;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);
	if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
		printf("UDP bind fail: %d\n", WSAGetLastError());
		closesocket(sock);
		sock = INVALID_SOCKET;
		return false;
	}
	u_long nonBlocking = 1;
	ioctlsocket(sock, FIONBIO, &nonBlocking);// 논블로킹 모드

	// 클라가 꺼진뒤에 sendto하면 다음 recvfrom이 WSAECONNRESET으로 실패하는 Windows 동작을 끔
	BOOL reportReset = FALSE;
	DWORD bytes = 0;
	WSAIoctl(sock, SIO_UDP_CONNRESET, &reportReset, sizeof(reportReset), nullptr, 0, &bytes, nullptr, nullptr);// 소켓 정보 조회
	return true;
}

SOCKET UdpEndpoint::GetSocket() const { return sock; }

void UdpEndpoint::OnReadable(EventQueue<RecvEvent>&recvQ) {
	while (true) {
		char buf[MAX_UDP_SIZE];
		sockaddr_in from{};
		int fromLen = sizeof(from);
		int len = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&from, &fromLen);
		if (len == SOCKET_ERROR) {
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK)break;// 더 읽을게 없음
			if (err == WSAECONNRESET || err == WSAEMSGSIZE)continue;// 이 패킷만 버리고 계속함
			printf("recvfrom fail: %d\n", err);
			return;
		}

		if (len < (int)sizeof(PacketHeader))continue;
		const PacketHeader* h = reinterpret_cast<const PacketHeader*>(buf);
		if (h->size != len)continue;

		switch (h->type) {
		case C2S_UDP_HELLO:
			if (len == sizeof(C2S_UdpHello))
				OnHello(*reinterpret_cast<const C2S_UdpHello*>(buf), from);
			break;
		case C2S_INPUT:{
			int id = FindPlayer(from);
			if (id < 0 || len != sizeof(C2S_Input))break;// 등록 안된 주소는 버림
			recvQ.Push(RecvEvent{ RecvKind::Packet,(uint8_t)id,std::vector<char>(buf,buf + len) });
			break;
		}
		}
	}
}

void UdpEndpoint::SendTo(int playerId, const char* data, int len) {
	if (!IsRegistered(playerId))return;
	sendto(sock, data, len, 0, (const sockaddr*)&peers[playerId].addr, sizeof(sockaddr_in));// UDP라 실패해도 무시
}

bool UdpEndpoint::IsRegistered(int playerId)const {
	if (playerId < 0 || playerId >= MAX_PLAYERS)return false;
	return peers[playerId].registered;
}

void UdpEndpoint::SetToken(int playerId, uint32_t token) {
	if (playerId < 0 || playerId >= MAX_PLAYERS)return;
	peers[playerId] = Peer{};
	peers[playerId].token = token;
}

void UdpEndpoint::Unregister(int playerId) {
	if (playerId < 0 || playerId >= MAX_PLAYERS)return;
	peers[playerId] = Peer{};
}

void UdpEndpoint::OnHello(const C2S_UdpHello& pkt, const sockaddr_in& from) {
	int id = pkt.playerId;
	if (id >= MAX_PLAYERS)return;
	Peer& p = peers[id];
	if (p.token == 0 || pkt.token != p.token)return;//  로그인 안했거나 토큰이 틀리면 무시
	if (!p.registered)printf("UDP register: P%d port %d\n", id, ntohs(from.sin_port));
	p.addr = from;
	p.registered = true;// 클라가 HELLO를 반복해서 보내므로 매번 최신 주소로 갱신
}

int UdpEndpoint::FindPlayer(const sockaddr_in& from)const {
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		const Peer& p = peers[i];
		if (!p.registered)continue;
		if (p.addr.sin_addr.s_addr == from.sin_addr.s_addr && p.addr.sin_port == from.sin_port)
			return i;
	}
	return -1;
}