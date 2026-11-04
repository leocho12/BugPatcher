#include "World.h"
#include <algorithm>

constexpr float PLAYER_SPEED = 250.0f;// px/초
constexpr float PLAYER_STEP = PLAYER_SPEED * TICK_MS / 1000.0f; // 한 틱당 이동량

void World::Init(EventQueue<RecvEvent>* rq, EventQueue<SendEvent>* sq,
	HANDLE ev, const GameStartInfo* info) {
	recvQ = rq;
	sendQ = sq;
	startEvent = ev;
	startInfo = info;
}
DWORD WINAPI World::ThreadMain(LPVOID) { return 0; }

void World::OnInput(int playerId, const C2S_Input& pkt) {
	if (playerId < 0 || playerId >= MAX_PLAYERS) return;
	ServerPlayer& p = players[playerId];
	if (pkt.seq <= p.lastseq)return;// UDP를 쓰면 패킷이 늦게 도착할 수 있으므로 시퀀스 번호를 보고 이전 패킷이면 무시
	p.lastseq = pkt.seq;
	p.lastkeys = pkt.keys;
	p.aimAngle = pkt.aimAngle;
}

void World::OnSelectWeapon(int,const C2S_SelectWeapon&){}
void World::OnDisconnect(int) {}

void World::SpawnPlayer() {
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		players[i] = ServerPlayer{};
		players[i].active = true;
		players[i].x = MAP_WIDTH / 2.0f + (i == 0 ? -50.0f : 50.0f);// 겹치지 않게 맵 중앙의 좌우에서 스폰
		players[i].y = MAP_HEIGHT / 2.0f;
	}
}

void World::UpdatePlayers() {
	for (ServerPlayer& p:players) {
		if (!p.active || p.hp <= 0)continue;
		if (p.lastkeys & KEY_A)p.x -= PLAYER_STEP;
		if (p.lastkeys & KEY_D)p.x += PLAYER_STEP;
		if (p.lastkeys & KEY_W)p.y -= PLAYER_STEP;
		if (p.lastkeys & KEY_S)p.y += PLAYER_STEP;

		// 맵 경계 체크
		p.x = std::clamp(p.x, 0.0f, (float)MAP_WIDTH);
		p.y = std::clamp(p.y, 0.0f, (float)MAP_HEIGHT);
	}
}