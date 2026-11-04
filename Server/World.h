#pragma once
#include <WinSock2.h>
#include <Windows.h>
#include "EventQueue.h"
#include "Events.h"
struct C2S_Input;
struct C2S_SelectWeapon;

// 서버가 들고있는 플레이어 1명의 상태
struct ServerPlayer {
	bool active = false;// 현재 접속 상태인지
	float x = 0, y = 0;// 현재 위치(위치는 float로 계산하고 패킷 크기를 줄이기 위해 스냅샷을 보낼때는 int16으로 변환)
	int hp = PLAYER_MAX_HP;
	uint8_t lastkeys = 0;// 마지막으로 받은 키 입력
	float aimAngle = 0;
	uint32_t lastseq = 0;// 마지막으로 받은 입력 패킷의 시퀀스 번호(늦게 도착한건 버림)
};

class World {
public:
	void Init(EventQueue<RecvEvent>* recvQ, EventQueue<SendEvent>* sendQ,
		HANDLE startEvent, const GameStartInfo* startInfo);
	static DWORD WINAPI ThreadMain(LPVOID self);
	void OnInput(int playerId, const C2S_Input& pkt);
	void OnSelectWeapon(int playerId, const C2S_SelectWeapon& pkt);
	void OnDisconnect(int playerId);
	void SpawnPlayer();// 게임 시작 시 위치, HP 초기화
	void UpdatePlayers();// 한 틱만큼 이동
private:
	EventQueue<RecvEvent>* recvQ = nullptr;		
	EventQueue<SendEvent>* sendQ = nullptr;		
	HANDLE startEvent = nullptr;				
	const GameStartInfo* startInfo = nullptr;
	ServerPlayer players[MAX_PLAYERS];
};