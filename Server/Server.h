#pragma once
#include <WinSock2.h>
#include <Windows.h>
#include "Protocol.h"
#include "EventQueue.h"
#include "Events.h"
#include "Session.h"
#include "UdpEndpoint.h"
#include "World.h"

// 서버 메인 스레드: TCP 리슨, WSAPoll 루프, 로비 처리, 송신 큐 비우기
class Server {
public:
	bool Init();		
	void Run();			
	void Shutdown();

	void ProcessPacket(Session& s, const PacketHeader* pkt);

private:
	void OnAccept();
	void OnLogin(Session& s, const C2S_Login& pkt);
	void OnMapSelect(Session& s, const C2S_MapSelect& pkt);
	void SendLoginOk(Session& s);
	void SendUdpOk(Session& s);
	void CheckGameStart();			
	void FlushSendQueue();			
	void Disconnect(Session& s);	

	SOCKET listenSock = INVALID_SOCKET;
	Session sessions[MAX_PLAYERS];
	UdpEndpoint udp;
	World world;

	EventQueue<RecvEvent> recvQ;	
	EventQueue<SendEvent> sendQ;
	std::vector<SendEvent> sendBuf;	// FlushSendQueue에서 송신 큐를 꺼내 담는 곳
	HANDLE gameStartEvent = nullptr;	
	HANDLE worldThread = nullptr;
	GameStartInfo startInfo{};
	bool gameStarted = false;
};
