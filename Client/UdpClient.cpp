#include "UdpClient.h"
#include <WS2tcpip.h>
#include <mswsock.h>
#include "Protocol.h"
#include "SnapshotBuffer.h"

bool UdpClient::Start(const char* serverIp, uint8_t id, uint32_t tk, SnapshotBuffer* sb) {
	playerId = id;
	token = tk;
	snap = sb;
	seq = 0;

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET) return false;

	// 서버가 꺼져 있을 때 recvfrom이 WSAECONNRESET으로 실패하는 동작을 끔 (서버와 같은 설정)
	BOOL reportReset = FALSE;
	DWORD bytes = 0;
	WSAIoctl(sock, SIO_UDP_CONNRESET, &reportReset, sizeof(reportReset), nullptr, 0, &bytes, nullptr, nullptr);

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(SERVER_UDP_PORT);
	if (inet_pton(AF_INET, serverIp, &serverAddr.sin_addr) != 1) {
		closesocket(sock);
		sock = INVALID_SOCKET;
		return false;
	}

	udpOkEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);// 수동 리셋, 처음엔 꺼짐
	if (udpOkEvent == nullptr) { Stop(); return false; }
	running = true;
	thread = CreateThread(nullptr, 0, ThreadMain, this, 0, nullptr);
	if (thread == nullptr) { Stop(); return false; }
	return true;
}

DWORD WINAPI UdpClient::ThreadMain(LPVOID self) {
	static_cast<UdpClient*>(self)->Run();
	return 0;
}

void UdpClient::OnUdpOk() {
	if (udpOkEvent) SetEvent(udpOkEvent);
}

bool UdpClient::IsReady() const {
	return udpOkEvent && WaitForSingleObject(udpOkEvent, 0) == WAIT_OBJECT_0;// 0ms: 상태만 확인
}

void UdpClient::SendHello() {
	C2S_UdpHello pkt{};
	InitPacket(pkt, C2S_UDP_HELLO);
	pkt.playerId = playerId;
	pkt.token = token;
	sendto(sock, (const char*)&pkt, sizeof(pkt), 0, (const sockaddr*)&serverAddr, sizeof(serverAddr));
}

void UdpClient::Run() {
	// 1. 등록: HELLO를 보내고 UDP_OK 신호를 200ms 기다리기를 반복
	while (running) {
		SendHello();
		if (WaitForSingleObject(udpOkEvent, 200) == WAIT_OBJECT_0) break;
	}

	// 2. 수신: 스냅샷 조각을 받아 SnapshotBuffer에 넘김
	char buf[MAX_UDP_SIZE];
	while (running) {
		sockaddr_in from{};
		int fromLen = sizeof(from);
		int len = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&from, &fromLen);
		if (len == SOCKET_ERROR) {
			int err = WSAGetLastError();
			if (err == WSAECONNRESET || err == WSAEMSGSIZE) continue;// 이 패킷만 버림
			break;// Stop()의 closesocket으로 깨어난 경우도 여기
		}
		if (from.sin_addr.s_addr != serverAddr.sin_addr.s_addr || from.sin_port != serverAddr.sin_port) continue;// 서버가 아닌 곳에서 온 패킷
		if (len < (int)sizeof(S2C_SnapshotHead)) continue;
		const PacketHeader* h = reinterpret_cast<const PacketHeader*>(buf);
		if (h->size != len || h->type != S2C_SNAPSHOT) continue;
		if (snap) snap->WriteFragment(buf, len);
	}
}

void UdpClient::SendInput(uint8_t keys, float aimAngle) {
	if (!IsReady()) return;// 등록 전 입력은 서버가 어차피 버림
	C2S_Input pkt{};
	InitPacket(pkt, C2S_INPUT);
	pkt.seq = ++seq;// 1부터 시작해야 서버의 lastseq(0) 검사를 통과
	pkt.keys = keys;
	pkt.aimAngle = aimAngle;
	sendto(sock, (const char*)&pkt, sizeof(pkt), 0, (const sockaddr*)&serverAddr, sizeof(serverAddr));
}

void UdpClient::Stop() {
	running = false;
	if (udpOkEvent) SetEvent(udpOkEvent);// HELLO 대기 깨우기
	if (sock != INVALID_SOCKET) { closesocket(sock); sock = INVALID_SOCKET; }// recvfrom 깨우기
	if (thread) { WaitForSingleObject(thread, INFINITE); CloseHandle(thread); thread = nullptr; }
	if (udpOkEvent) { CloseHandle(udpOkEvent); udpOkEvent = nullptr; }
}