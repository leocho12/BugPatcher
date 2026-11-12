#pragma once

constexpr int NET_MAX_PLAYERS = 2;

struct NetPlayerView {
	int x, y;
	int hp;
	float aimAngle;
};

// 완성된 스냅샷이 있으면 true. 메인(UI) 스레드에서만 호출
bool NetGetPlayers(NetPlayerView out[NET_MAX_PLAYERS]);