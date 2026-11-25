#pragma once
#include "GameTypes.h"

struct BossDef { int hp;float speed;int contactDamage;uint32_t spawnAtMs; };
constexpr BossDef BOSS_DEFS[4] = {/*hp,speed,contactDamage,spawntime*/
	{0,0,0,0},// dummy
	{1000,93.75f,5,120000},// boss1
	{800,93.75f,20,180000},//boss2
	{1200,75.0f,10,240000} };// boss3
constexpr float BOSS_RADIUS = 64, PLAYER_RADIUS = 16, BULLET_RADIUS = 8;
constexpr int   BULLET_DAMAGE = 10;
constexpr int   RING_COUNT = 8;  constexpr float RING_SPEED = 312.5f;
constexpr uint32_t RING_INTERVAL_1 = 3000, RING_INTERVAL_3 = 2500;
constexpr uint32_t AIM_INTERVAL = 2000;   constexpr float AIM_SPEED = 1875.0f;
constexpr uint32_t DASH_INTERVAL = 10000, DASH_DURATION = 1000;  constexpr float DASH_SPEED = 500.0f;
constexpr uint32_t AOE_COOLDOWN = 5000, AOE_STEP_MS = 300;
constexpr int AOE_STEPS = 3, AOE_BASE = 150, AOE_GROW = 50, AOE_DAMAGE = 20;
constexpr size_t MAX_BOSS_BULLETS = 128;  // 스냅샷 크기 보호용 상한