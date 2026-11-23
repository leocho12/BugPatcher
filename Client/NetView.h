#pragma once
#include <vector>

constexpr int NET_MAX_PLAYERS = 2;

struct NetPlayerView {
	int x, y;
	int hp;
	float aimAngle;
	unsigned int weaponActiveBits;	// 지금 이펙트를 보일 무기.
};

struct NetEnemyView {
	int id;
	int type;		// 0 일반, 1 빠름, 2 탱크 (GameTypes.h EnemyType과 같은 순서)
	int x, y;
};

// 완성된 스냅샷이 있으면 true. 메인(UI) 스레드에서만 호출
bool NetGetState(NetPlayerView players[NET_MAX_PLAYERS], std::vector<NetEnemyView>& enemies);