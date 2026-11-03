#pragma once
#include <WinSock2.h>
#include <Windows.h>
#include <cstdint>
#include <vector>
#include "Protocol.h"


constexpr UINT WM_NET_PACKET = WM_APP + 1;
constexpr UINT WM_NET_DISCONNECTED = WM_APP + 2;

class NetworkClient {
public:
	bool Connect(const char* serverIp, HWND hwnd);	// TCP connect + 수신 스레드 시작
	void Close();

	void SendLogin(const char* nickname);
	void SendMapSelect(uint8_t mapId);
	void SendSelectWeapon(uint8_t cardIndex);

private:
	static DWORD WINAPI RecvThread(LPVOID self);
	bool Send(const void* data, int len);			// 다 보낼 때까지 send 반복
	void HandlePacket(const PacketHeader* pkt);		// 완성된 패킷 -> PostMessage

	SOCKET sock = INVALID_SOCKET;
	HWND hwnd = nullptr;
	HANDLE recvThread = nullptr;
	std::vector<char> recvBuf;
};