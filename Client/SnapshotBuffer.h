#pragma once
#include <Windows.h>
#include <vector>
#include "../Common/GameTypes.h"

struct Snapshot {
	uint32_t tick = 0;
	PlayerState player[MAX_PLAYERS]{};
	std::vector<EnemyState> enemies;
	std::vector<BossState> bosses;
	std::vector<BulletState> bullets;
	std::vector<GimmickState> gimmicks;
};

class SnapshotBuffer {
public:
	SnapshotBuffer();
	// 임계영역
	~SnapshotBuffer();

	void WriteFragment(const char* data, int len);// UDP 수신 스레드가 호출
	void CopyTo(Snapshot& out);// WM_PAINT가 호출
private:
	CRITICAL_SECTION cs;
	Snapshot current;
};