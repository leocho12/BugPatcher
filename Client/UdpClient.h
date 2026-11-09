#pragma once
#include <WinSock2.h>
#include <Windows.h>
#include <atomic>
#include <cstdint>
class SnapshotBuffer;

// 클라 UDP: HELLO로 주소 등록 -> UDP_OK 신호 대기 -> 스냅샷 수신, 입력 송신
class UdpClient {
public:
	bool Start(const char* serverIp, uint8_t playerId, uint32_t token, SnapshotBuffer* snap);
	void OnUdpOk();// TCP 수신 스레드가 S2C_UDP_OK를 받으면 호출
	bool IsReady() const;// UDP 등록이 끝났는지
	void SendInput(uint8_t keys, float aimAngle);// UI 스레드, 33ms마다
	void Stop();
private:
	static DWORD WINAPI ThreadMain(LPVOID self);
	void Run();
	void SendHello();

	SOCKET sock = INVALID_SOCKET;
	sockaddr_in serverAddr{};
	uint8_t playerId = 0;
	uint32_t token = 0;
	uint32_t seq = 0;// SendInput은 UI 스레드에서만 부르므로 잠그지 않음
	SnapshotBuffer* snap = nullptr;
	HANDLE udpOkEvent = nullptr;// 수동 리셋 Event
	HANDLE thread = nullptr;
	std::atomic<bool> running{ false };
};