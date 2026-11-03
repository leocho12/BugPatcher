#pragma once
#include <WinSock2.h>
#include <Windows.h>
#include "EventQueue.h"
#include "Events.h"
struct C2S_Input;
struct C2S_SelectWeapon;

class World {
public:
	void Init(EventQueue<RecvEvent>* recvQ, EventQueue<SendEvent>* sendQ,
		HANDLE startEvent, const GameStartInfo* startInfo);
	static DWORD WINAPI ThreadMain(LPVOID self);
	void OnInput(int playerId, const C2S_Input& pkt);
	void OnSelectWeapon(int playerId, const C2S_SelectWeapon& pkt);
	void OnDisconnect(int playerId);
private:
	EventQueue<RecvEvent>* recvQ = nullptr;		
	EventQueue<SendEvent>* sendQ = nullptr;		
	HANDLE startEvent = nullptr;				
	const GameStartInfo* startInfo = nullptr;	
};