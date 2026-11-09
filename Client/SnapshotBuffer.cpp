#include "SnapshotBuffer.h"
#include <cstring>
#include "Protocol.h"

namespace {
	// 길이를 확인하면서 앞에서부터 읽기. 범위를 넘으면 실패
	struct Reader {
		const char* p;
		const char* end;
		template<class T> bool Read(T& out) {
			if (end - p < (ptrdiff_t)sizeof(T)) return false;
			memcpy(&out, p, sizeof(T));
			p += sizeof(T);
			return true;
		}
		template<class T> bool ReadArray(std::vector<T>& v, size_t n) {
			if ((size_t)(end - p) < n * sizeof(T)) return false;
			size_t old = v.size();
			v.resize(old + n);
			if (n > 0) memcpy(v.data() + old, p, n * sizeof(T));
			p += n * sizeof(T);
			return true;
		}
	};
}

SnapshotBuffer::SnapshotBuffer() { InitializeCriticalSection(&cs); }
SnapshotBuffer::~SnapshotBuffer() { DeleteCriticalSection(&cs); }

void SnapshotBuffer::WriteFragment(const char* data, int len) {
	if (len < (int)sizeof(S2C_SnapshotHead)) return;
	S2C_SnapshotHead head;
	memcpy(&head, data, sizeof(head));
	if (head.fragCount == 0 || head.fragCount > 32 || head.fragIndex >= head.fragCount) return;

	if (head.tick <= lastPublished) return;// 이미 완성한 것보다 오래된 조각 (UDP 순서 뒤바뀜)
	if (head.tick != building.tick) {// 새 틱 시작. 덜 모인 이전 틱은 버림
		building.tick = head.tick;
		building.enemies.clear();
		building.bosses.clear();
		building.bullets.clear();
		building.gimmicks.clear();
		buildingMask = 0;
		buildingCount = head.fragCount;
	}
	if (head.fragCount != buildingCount) return;// 같은 틱인데 조각 수가 다르면 잘못된 패킷
	uint32_t bit = 1u << head.fragIndex;
	if (buildingMask & bit) return;// 중복 조각

	if (!ParseFragment(data + sizeof(head), len - (int)sizeof(head), head.fragIndex == 0)) {
		building.tick = 0;// 깨진 조각이면 이 틱은 포기
		return;
	}
	buildingMask |= bit;

	uint32_t full = (buildingCount == 32) ? 0xFFFFFFFFu : ((1u << buildingCount) - 1);
	if (buildingMask == full) Publish();
}

bool SnapshotBuffer::ParseFragment(const char* data, int len, bool first) {
	Reader r{ data, data + len };
	if (!r.Read(building.player)) return false;// 모든 조각에 같은 값이 들어 있음

	if (first) {// 0번 조각만 보스, 탄막, 기믹
		uint8_t bossCount = 0;
		uint16_t bulletCount = 0;
		uint8_t gimmickCount = 0;
		if (!r.Read(bossCount) || !r.ReadArray(building.bosses, bossCount)) return false;
		if (!r.Read(bulletCount) || !r.ReadArray(building.bullets, bulletCount)) return false;
		if (!r.Read(gimmickCount) || !r.ReadArray(building.gimmicks, gimmickCount)) return false;
	}
	uint16_t enemyCount = 0;
	if (!r.Read(enemyCount) || !r.ReadArray(building.enemies, enemyCount)) return false;
	return r.p == r.end;// 남는 바이트가 있으면 형식이 틀린 것
}

void SnapshotBuffer::Publish() {
	lastPublished = building.tick;
	EnterCriticalSection(&cs);
	current.tick = building.tick;
	memcpy(current.player, building.player, sizeof(current.player));
	current.enemies.swap(building.enemies);// 벡터는 복사하지 않고 교체해 잠금 시간을 줄임
	current.bosses.swap(building.bosses);
	current.bullets.swap(building.bullets);
	current.gimmicks.swap(building.gimmicks);
	LeaveCriticalSection(&cs);
	building.tick = 0;// 다음 틱 조립 준비 (옛 벡터는 다음 틱 시작 때 비움)
}

bool SnapshotBuffer::CopyTo(Snapshot& out) {
	EnterCriticalSection(&cs);
	bool has = current.tick != 0;
	if (has) out = current;// out의 벡터 용량을 재사용하므로 매 프레임 할당이 적음
	LeaveCriticalSection(&cs);
	return has;
}