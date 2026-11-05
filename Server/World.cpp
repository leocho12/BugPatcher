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

DWORD WINAPI World::ThreadMain(LPVOID self) {
	static_cast<World*>(self)->Run();// CreateThread용 정적 함수는 넘겨받기만
	return 0;
}

void World::OnInput(int playerId, const C2S_Input& pkt) {
	if (playerId < 0 || playerId >= MAX_PLAYERS) return;
	ServerPlayer& p = players[playerId];
	if (pkt.seq <= p.lastseq)return;// UDP를 쓰면 패킷이 늦게 도착할 수 있으므로 시퀀스 번호를 보고 이전 패킷이면 무시
	p.lastseq = pkt.seq;
	p.lastkeys = pkt.keys;
	p.aimAngle = pkt.aimAngle;
}

void World::OnSelectWeapon(int,const C2S_SelectWeapon&){}
void World::OnDisconnect(int playerId) {
	if (playerId < 0 || playerId >= MAX_PLAYERS)return;
	players[playerId].active = false;
}

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

void World::Run() {
	WaitForSingleObject(startEvent, INFINITE);// 게임 시작 신호를 기다림
	if (!running)return;// 시작전에 서버가 꺼지면 종료

	SpawnPlayer();

	ULONGLONG nextTick = GetTickCount64();
	while (running) {
		ULONGLONG now = GetTickCount64();
		if (now < nextTick) {
			Sleep((DWORD)(nextTick - now));// 다음 틱까지 잠
			continue;
		}
		nextTick += TICK_MS;// now가 아니라 예정 시각에 더해야 오차가 쌓이지 않음

		ProcessRecvEvents();
		Update(TICK_MS);
	}
}

void World::Update(uint32_t dtMs) {
	nowMs += dtMs;
	UpdatePlayers();
}

void World::ProcessRecvEvents() {
	recvQ->SwapOut(recvBuf);// 쓰레드가 쌓아논 이벤트 가져옴

	for (const RecvEvent& e : recvBuf) {// 이밴트 하나씩 꺼내기
		if (e.kind == RecvKind::Disconnect) {
			OnDisconnect(e.playerId);
			continue;
		}
		if (e.data.size() < sizeof(PacketHeader))continue;// 너무 작은 패킷은 무시
		
		const PacketHeader* h = reinterpret_cast<const PacketHeader*>(e.data.data());// 패킷 헤더를 꺼내서 타입 확인
		switch (h->type) {
		case C2S_INPUT:// 키 입력 패킷인 경우
			if (e.data.size() == sizeof(C2S_Input))
				OnInput(e.playerId, *reinterpret_cast<const C2S_Input*>(h));
			break;
		case C2S_SELECT_WEAPON:// 무기 선택 패킷인 경우
			if (e.data.size() == sizeof(C2S_SelectWeapon))
				OnSelectWeapon(e.playerId, *reinterpret_cast<const C2S_SelectWeapon*>(h));
			break;
		}
	}
}