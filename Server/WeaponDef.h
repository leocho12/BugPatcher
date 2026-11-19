#pragma once
#include <cstdint>
#include "GameTypes.h"

// 무기 수치 표. 원작 BugPather.cpp에 흩어져 있던 수치를 한 곳에 모았다. 2인 밸런스 조정은 이 파일만 고치면 된다.
// 시간은 모두 게임 시간(ms)이고, 판정은 World::Update의 33ms 틱 안에서 tickMs마다 처리한다.
struct WeaponDef {
	uint32_t cooldownMs;	// 발동 간격. 0이면 쿨타임 없이 항상 켜져 있음
	uint32_t durationMs;	// 한 번 발동하면 켜져 있는 시간 (cooldownMs가 0이면 쓰지 않음)
	uint32_t tickMs;		// 켜져 있는 동안 판정하는 간격
	int baseDamage;			// 1레벨 피해
	int damagePerLevel;		// 강화 1번마다 늘어나는 피해
};

// 인덱스는 GameTypes.h의 WeaponType 순서와 같다: SLOW, CIRCLE, BREATH, SLASH, HOMING
constexpr WeaponDef WEAPON_DEFS[WEAPON_COUNT] = {
	/* SLOW   홈매트   */ { 0,    0,    TICK_MS, 0,  0 },	// 상시. 피해 없이 범위 안의 적을 느리게 함
	/* CIRCLE 전기장   */ { 0,    0,    100,     1,  1 },	// 상시. 100ms마다 범위 피해 (원작 타이머 12)
	/* BREATH 에프킬라 */ { 5000, 2500, 100,     5,  1 },	// 5초마다 2.5초 동안, 100ms마다 부채꼴 피해 (원작 타이머 13)
	/* SLASH  파리채   */ { 1000, 200,  16,      3,  2 },	// 1초마다 0.2초 동안, 16ms마다 판정 (원작 타이머 11). 처음부터 가지고 시작
	/* HOMING 소금총   */ { 5000, 3000, TICK_MS, 10, 1 },	// 5초마다 발사, 최대 3초 동안 날아감 (원작 타이머 15)
};

constexpr float PI_F = 3.14159265358979f;

// 파리채: 플레이어 앞 SLASH_OFFSET 지점을 중심으로 가로 128 x 세로 32 사각형이 조준 방향으로 회전한다
constexpr float SLASH_OFFSET = 64.0f;
constexpr float SLASH_WIDTH = 128.0f;
constexpr float SLASH_HEIGHT = 32.0f;
// 원작은 적 판정 사각형을 (x-16, y-16)에서 시작하는 64x64로 쓴다(중심 기준 32x32가 아님). 이식이라 그대로 둔다. 2인 밸런스 조정 때 줄일 수 있다
constexpr float SLASH_ENEMY_BOX_OFFSET = 16.0f;
constexpr float SLASH_ENEMY_BOX_SIZE = 64.0f;

constexpr float CIRCLE_RADIUS = 75.0f;				// 전기장 반지름
constexpr float SLOW_RADIUS = 100.0f;				// 홈매트 반지름
constexpr uint32_t SLOW_HOLD_MS = 2 * TICK_MS;		// 홈매트가 한 번 걸어 주는 둔화 유지 시간 (원작 +32ms = 2틱을 새 틱 기준으로)
constexpr float BREATH_RADIUS = 120.0f;				// 에프킬라 반지름
constexpr float BREATH_HALF_ANGLE = PI_F / 6.0f;	// 에프킬라는 조준 방향 좌우 30도(합 60도)
constexpr float HOMING_SPEED = 2500.0f;				// 소금총 탄 속도 px/초 (원작 16ms당 40px)
constexpr float HOMING_HIT_RADIUS = 20.0f;			// 탄이 적에게 닿았다고 보는 거리

// 레벨별 피해. 레벨 0은 아직 없는 무기라 0
inline int WeaponDamage(int type, int level) {
	if (level <= 0) return 0;
	const WeaponDef& d = WEAPON_DEFS[type];
	return d.baseDamage + d.damagePerLevel * (level - 1);
}