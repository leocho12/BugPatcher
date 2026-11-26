#pragma once
#include "GameTypes.h"

constexpr uint8_t  GIMMICK_MAP_ID = 1;			// 기믹 맵 번호
constexpr uint32_t CD_SPAWN_MS = 8000;
constexpr uint32_t FAN_SPAWN_MS = 8000;
constexpr uint32_t FAN_DURATION_MS = 30000;

constexpr float CD_SPEED = 375.0f;				// px/초
constexpr float CD_RADIUS = 64.0f;
constexpr int   CD_DAMAGE = 15;
constexpr float CD_OUT_MARGIN = 50.0f;			// 맵 밖으로 이만큼 나가면 제거

constexpr float FAN_MOVE_SPEED = 250.0f;		// 바람이 움직이는 속도
constexpr float FAN_PUSH_SPEED = 500.0f;		// 밀어내는 속도
constexpr float FAN_HALF_WIDTH = 64.0f;			// 바람 방향의 옆쪽 범위
constexpr float FAN_HALF_LENGTH = 100.0f;		// 바람 방향의 앞뒤 범위
constexpr float FAN_OUT_MARGIN = 200.0f;

constexpr size_t MAX_GIMMICKS = 8;				// 스냅샷 크기 보호용 상한