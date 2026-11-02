#pragma once
#include <cstdint>

// 공용 상수
constexpr int MAP_WIDTH = 2400;
constexpr int MAP_HEIGHT = 2400;
constexpr int VIEW_WIDTH = 800;
constexpr int VIEW_HEIGHT = 800;
constexpr int MAX_PLAYERS = 2;
constexpr int PLAYER_MAX_HP = 200;

constexpr uint16_t SERVER_TCP_PORT = 9000;
constexpr uint16_t SERVER_UDP_PORT = 9000;
constexpr uint32_t TICK_MS = 33;
constexpr int MAX_UDP_SIZE = 1400;

// 여러가지 enum 정의
enum WeaponType : uint8_t {
	WEAPON_SLOW,
	WEAPON_CIRCLE,
	WEAPON_BREATH,
	WEAPON_SLASH,
	WEAPON_HOMING,
	WEAPON_COUNT
};
enum EnemyType : uint8_t {
	ENEMY_NORMAL,
	ENEMY_FAST,
	ENEMY_TANK
};
enum BossType : uint8_t {
	BOSS_NONE,
	BOSS_BASIC,
	BOSS_DASH_SHOOT,
	BOSS_DASH_AND_BULLET
};
enum GimmickKind : uint8_t {
	GIMMICK_CD,
	GIMMICK_FAN
};

// 스냅샷에 담을 정보
#pragma pack(push,1)
struct PlayerState {
	uint8_t id;
	int16_t x, y, hp;
	uint8_t level;
	uint16_t killCount;
	float aimAngle;
	uint8_t weaponLevel[WEAPON_COUNT];
	uint8_t weaponActiveBits;
	uint8_t homingActive;
	int16_t homingX, homingY;
};
struct EnemyState {
	uint16_t id;
	uint8_t type;
	int16_t x, y;
};
struct BossState { uint8_t type;int16_t x, y, hp, maxHp;uint8_t aoeStep; };
struct BulletState { int16_t x, y;};
struct GimmickState { uint8_t kind; int16_t x, y; int8_t dirX, dirY; };
#pragma pack(pop)