#pragma once
#include <WinSock2.h>
#include <Windows.h>
#include <atomic>
#include <random>
#include <vector>
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

// 서버가 들고있는 적 1마리의 상태
struct ServerEnemy {
	uint16_t id = 0;// 고유 번호. EnemyState.id와 같은 크기
	uint8_t type = ENEMY_NORMAL;// EnemyType
	float x = 0, y = 0;
	int hp = 0;
	uint64_t slowUntil = 0;// 게임 시간(nowMs)이 이 값보다 작으면 슬로우 상태
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
	void SpawnEnemiesPerTick();// 이번 틱에 만들 적 수를 정해 생성 (1초마다 1~10마리, 나머지 틱은 0마리)
	void UpdateEnemies();// 적을 한 틱만큼 이동
	void Update(uint32_t dtMs);// 한 틱 시뮬레이션
	void SendSnapshot();// 현재 상태를 S2C_SNAPSHOT으로 만들어 송신 큐에 넣음
	void Stop(){ running = false; }// 서버 종료시 메인이 호출
private:
	EventQueue<RecvEvent>* recvQ = nullptr;		
	EventQueue<SendEvent>* sendQ = nullptr;		
	HANDLE startEvent = nullptr;				
	const GameStartInfo* startInfo = nullptr;
	ServerPlayer players[MAX_PLAYERS];
	void Run();
	void ProcessRecvEvents();
	void SpawnEnemy();// 적 1마리 생성
	const ServerPlayer* PickTarget(const ServerEnemy& e) const;// 적이 쫓아갈 플레이어

	std::vector<ServerEnemy> enemies;
	uint16_t nextEnemyId = 1;
	uint64_t lastSpawnMs = 0;// 마지막으로 적을 만든 게임 시간
	std::mt19937 rng{ std::random_device{}() };// 월드 스레드만 사용
	uint64_t nowMs = 0;// 게임 시간. 틱마다 TICK_MS씩 누적
	uint32_t tick = 0;// 스냅샷 번호
	std::atomic<bool> running{ true };
	std::vector<RecvEvent> recvBuf;
};
