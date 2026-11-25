#pragma once
#include <vector>

constexpr int NET_MAX_PLAYERS = 2;

struct NetPlayerView {
	int x, y;
	int hp;
	float aimAngle;
	unsigned int weaponActiveBits;	// 지금 이펙트를 보일 무기.
	int level;			// 현재 레벨
	int killCount;		// 누적 킬 수 (마지막으로 때린 적만 센다)
	bool homingActive;	// 소금총 탄이 날아가는 중인지
	int homingX, homingY; // 소금총 탄 위치
};

struct NetEnemyView {
	int id;
	int type;		// 0 일반, 1 빠름, 2 탱크 (GameTypes.h EnemyType과 같은 순서)
	int x, y;
};

// 완성된 스냅샷이 있으면 true. 메인(UI) 스레드에서만 호출
bool NetGetState(NetPlayerView players[NET_MAX_PLAYERS], std::vector<NetEnemyView>& enemies);