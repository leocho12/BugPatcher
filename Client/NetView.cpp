#include "NetView.h"
#include "SnapshotBuffer.h"

extern SnapshotBuffer g_snapshot;
static_assert(NET_MAX_PLAYERS == MAX_PLAYERS, "플레이어 수가 GameTypes.h와 같아야 한다");
static_assert(ENEMY_NORMAL == 0 && ENEMY_FAST == 1 && ENEMY_TANK == 2, "적 종류 번호가 BugPather와 같아야 한다");
static_assert(WEAPON_SLOW == 0 && WEAPON_CIRCLE == 1 && WEAPON_BREATH == 2 && WEAPON_SLASH == 3,
	"무기 비트 번호가 BugPather의 WeaponType 순서와 같아야 한다");

bool NetGetState(NetPlayerView players[NET_MAX_PLAYERS], std::vector<NetEnemyView>& enemies) {
	static Snapshot snap;	
	if (!g_snapshot.CopyTo(snap)) return false;	

	for (int i = 0; i < MAX_PLAYERS; ++i) {
		players[i].x = snap.player[i].x;
		players[i].y = snap.player[i].y;
		players[i].hp = snap.player[i].hp;
		players[i].aimAngle = snap.player[i].aimAngle;
		players[i].weaponActiveBits = snap.player[i].weaponActiveBits;
		players[i].level = snap.player[i].level;
		players[i].killCount = snap.player[i].killCount;
		players[i].homingActive = snap.player[i].homingActive != 0;
		players[i].homingX = snap.player[i].homingX;
		players[i].homingY = snap.player[i].homingY;
	}

	enemies.clear();
	for (const EnemyState& e : snap.enemies)
		enemies.push_back(NetEnemyView{ e.id, e.type, e.x, e.y });
	return true;
}