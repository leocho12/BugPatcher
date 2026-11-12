#pragma once
#include <WinSock2.h>
#include <Windows.h>
#include <cstdint>
#include <vector>

struct PacketHeader;

// TCP 수신 스레드 -> 메인 스레드 (PostMessage)
constexpr UINT WM_NET_LOGIN_OK = WM_APP + 1;		
constexpr UINT WM_NET_UDP_OK = WM_APP + 2;
constexpr UINT WM_NET_DISCONNECTED = WM_APP + 3;
constexpr UINT WM_NET_GAME_START = WM_APP + 4;

class NetworkClient {
public:
	bool Connect(const char* serverIp, HWND hwnd);	
	void Close();

	void SendLogin(const char* nickname);
	void SendMapSelect(uint8_t mapId);
	void SendSelectWeapon(uint8_t cardIndex);

private:
	static DWORD WINAPI RecvThread(LPVOID self);
	bool OnRecv(const char* data, int len);			// 수신 버퍼에 붙이고 완성된 패킷마다 HandlePacket
	bool Send(const void* data, int len);			
	void HandlePacket(const PacketHeader* pkt);		// 패킷 -> PostMessage

	SOCKET sock = INVALID_SOCKET;
	HWND hwnd = nullptr;
	HANDLE recvThread = nullptr;
	std::vector<char> recvBuf;		// 수신 스레드 전용
};