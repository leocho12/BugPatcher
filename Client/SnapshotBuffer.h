#pragma once
#include <Windows.h>
#include <vector>
#include "../Common/GameTypes.h"

struct Snapshot {
	uint32_t tick = 0;// 0이면 아직 받은 적 없음
	PlayerState player[MAX_PLAYERS]{};
	std::vector<EnemyState> enemies;
	std::vector<BossState> bosses;
	std::vector<BulletState> bullets;
	std::vector<GimmickState> gimmicks;
};

// UDP 수신 스레드가 조각을 조립하고, WM_PAINT가 완성본을 복사해 감
class SnapshotBuffer {
public:
	SnapshotBuffer();
	~SnapshotBuffer();

	void WriteFragment(const char* data, int len);// UDP 수신 스레드가 호출
	bool CopyTo(Snapshot& out);// WM_PAINT가 호출. 완성본이 없으면 false
private:
	bool ParseFragment(const char* data, int len, bool first);// building에 이어 붙임
	void Publish();// building -> current

	// 임계영역
	CRITICAL_SECTION cs;
	Snapshot current;// 완성본, cs로 보호

	Snapshot building;// 조립 중, UDP 스레드 전용이라 잠그지 않음
	uint32_t buildingMask = 0;// 받은 조각 번호 비트
	uint8_t buildingCount = 0;// 이 tick의 전체 조각 수
	uint32_t lastPublished = 0;// 마지막으로 완성한 tick, UDP 스레드 전용
};