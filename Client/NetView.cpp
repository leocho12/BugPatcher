#include "NetView.h"
#include "SnapshotBuffer.h"

extern SnapshotBuffer g_snapshot;
static_assert(NET_MAX_PLAYERS == MAX_PLAYERS, "플레이어 수가 GameTypes.h와 같아야 합니다.");

bool NetGetPlayers(NetPlayerView out[NET_MAX_PLAYERS]) {
	static Snapshot snap;
	if (!g_snapshot.CopyTo(snap)) return false;	

	for (int i = 0; i < MAX_PLAYERS; ++i) {
		out[i].x = snap.player[i].x;
		out[i].y = snap.player[i].y;
		out[i].hp = snap.player[i].hp;
		out[i].aimAngle = snap.player[i].aimAngle;
	}
	return true;
}