#pragma once
#include <WinSock2.h>
#include <Windows.h>
#include <atomic>
#include <random>
#include <vector>
#include "EventQueue.h"
#include "Events.h"
#include "WeaponDef.h"
struct C2S_Input;
struct C2S_SelectWeapon;

// 플레이어가 가진 무기 1개의 상태
struct WeaponState {
	uint8_t level = 0;// 0이면 아직 없는 무기
	bool active = false;// 지금 판정이 켜져 있는지 (화면에 이펙트를 보이는지)
	int64_t lastFireMs = 0;// 마지막으로 발동한 게임 시간 (쿨타임 기준)
	int64_t lastTickMs = 0;// 마지막으로 판정한 게임 시간 (tickMs 간격 기준)
};

// 소금총 탄 1발 (플레이어마다 최대 1발)
struct HomingState {
	bool active = false;
	float x = 0, y = 0;
	uint16_t targetId = 0;// 쫓는 적의 id (적이 지워져도 밀리지 않게 번호 대신 id로 찾는다)
};

// 서버가 들고있는 플레이어 1명의 상태
struct ServerPlayer {
	bool active = false;// 현재 접속 상태인지
	float x = 0, y = 0;// 현재 위치(위치는 float로 계산하고 패킷 크기를 줄이기 위해 스냅샷을 보낼때는 int16으로 변환)
	int hp = PLAYER_MAX_HP;
	uint8_t lastkeys = 0;// 마지막으로 받은 키 입력
	float aimAngle = 0;
	uint32_t lastseq = 0;// 마지막으로 받은 입력 패킷의 시퀀스 번호(늦게 도착한건 버림)
	WeaponState weapons[WEAPON_COUNT];// 무기별 상태 (WeaponType 순서)
	HomingState homing;// 소금총 탄
	int killCount = 0;// 이 플레이어가 마지막으로 때려서 잡은 적 수
	int level = 1;
	int levelKills = 0;// 현재 레벨에서 잡은 킬
	int pendingLevelUps = 0;// 카드 선택 중 올라간 레벨업 대기 수
	bool chossing = false;// 카드 선택 중
	uint8_t choice[LEVELUP_CHOICES] = {};
};

// 서버가 들고있는 적 1마리의 상태
struct ServerEnemy {
	uint16_t id = 0;// 고유 번호. EnemyState.id와 같은 크기
	uint8_t type = ENEMY_NORMAL;// EnemyType
	float x = 0, y = 0;
	int hp = 0;
	uint64_t slowUntil = 0;// 게임 시간(nowMs)이 이 값보다 작으면 슬로우 상태
	int8_t lastHitBy = -1;// 마지막으로 때린 플레이어 번호. 죽으면 그 플레이어가 킬을 얻는다
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
	void UpdateContactDamage();// 100ms마다 적과 닿은 플레이어 HP 감소
	void UpdateWeapons();// 플레이어별 무기 5종 판정과 킬 처리
	void GrantWeapon(int playerId, int weaponType);// 무기를 처음 얻거나 강화한다 (레벨업 카드 선택 효과)
	void Update(uint32_t dtMs);// 한 틱 시뮬레이션
	void SendSnapshot();// 현재 상태를 S2C_SNAPSHOT으로 만들어 송신 큐에 넣음
	void BroadcastGameStart();// 시작 위치를 정한 뒤 S2C_GAME_START를 두 클라에 보냄
	void Stop(){ running = false; }// 서버 종료시 메인이 호출

	// 무기 처리를 Update에 연결할지 여부. 11/20 2단계 확인(적) 이후에 true로 바꾼다.
	// 꺼져 있으면 무기가 적을 죽이지 않아, 무기 이펙트가 아직 없는 클라 화면에서 적이 이유 없이 사라지지 않는다
	bool weaponsEnabled = false;
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

	void ResetWeapons(ServerPlayer& p);// 게임 시작 시 무기 초기화 (파리채 1레벨만 가지고 시작)
	void DamageEnemy(ServerEnemy& e, int damage, int playerId);// 피해를 주고 마지막으로 때린 플레이어를 기록
	void ReapDeadEnemies();// HP가 0 이하인 적을 지우고 킬을 마지막으로 때린 플레이어에게 준다
	void OnKill(int playerId);
	void TryStartLevelUp(int playerId);
	void UpdateSlash(int playerId);// 파리채
	void UpdateCircle(int playerId);// 전기장
	void UpdateBreath(int playerId);// 에프킬라
	void UpdateSlow(int playerId);// 홈매트
	void UpdateHoming(int playerId);// 소금총
	int FindHomingTarget(const ServerPlayer& p) const;// 소금총이 노릴 적: 화면 안에서 HP가 가장 높은 적(같으면 가까운 적)의 번호, 없으면 -1

	std::vector<ServerEnemy> enemies;
	uint16_t nextEnemyId = 1;
	uint64_t lastSpawnMs = 0;// 마지막으로 적을 만든 게임 시간
	uint64_t lastContactMs = 0;// 마지막 접촉 판정 게임 시간
	std::mt19937 rng{ std::random_device{}() };// 월드 스레드만 사용
	uint64_t nowMs = 0;// 게임 시간. 틱마다 TICK_MS씩 누적
	uint32_t tick = 0;// 스냅샷 번호
	std::atomic<bool> running{ true };
	std::vector<RecvEvent> recvBuf;
};
