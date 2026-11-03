#pragma once
#include <cstdint>
#include "GameTypes.h"

// 패킷 종류. 0은 비워 두어 초기화 안 된 패킷을 구분한다
enum PacketType : uint8_t {
	C2S_LOGIN = 1,
	S2C_LOGIN_OK,
	C2S_UDP_HELLO,
	S2C_UDP_OK,
	C2S_MAP_SELECT,
	S2C_GAME_START,
	C2S_INPUT,
	S2C_SNAPSHOT,
	C2S_SELECT_WEAPON,
	S2C_LEVEL_UP,
	S2C_BOSS_ALERT,
	S2C_GAME_RESULT
};

// C2S_Input.keys 비트
enum InputKey : uint8_t {
	KEY_W = 1 << 0,
	KEY_A = 1 << 1,
	KEY_S = 1 << 2,
	KEY_D = 1 << 3
};

enum GameResult : uint8_t {
	RESULT_CLEAR,
	RESULT_GAMEOVER
};

constexpr int NICKNAME_LEN = 16;
constexpr int LEVELUP_CHOICES = 3;
constexpr int MAX_PACKET_SIZE = MAX_UDP_SIZE;	// 헤더 size가 이보다 크면 잘못된 패킷

#pragma pack(push, 1)
struct PacketHeader {
	uint16_t size;	// 헤더 포함 전체 바이트
	uint8_t type;	// PacketType
};

// 로비 (TCP, UDP_HELLO만 UDP)
struct C2S_Login {
	PacketHeader header;
	char nickname[NICKNAME_LEN];
};
struct S2C_LoginOk {
	PacketHeader header;
	uint8_t playerId;	// 0 / 1
	uint32_t token;		// C2S_UdpHello에 넣어 보낼 값
};
struct C2S_UdpHello {
	PacketHeader header;
	uint8_t playerId;
	uint32_t token;
};
struct S2C_UdpOk {
	PacketHeader header;
};
struct C2S_MapSelect {
	PacketHeader header;
	uint8_t mapId;
};
struct S2C_GameStart {
	PacketHeader header;
	uint8_t mapId;
	int16_t startX[MAX_PLAYERS];
	int16_t startY[MAX_PLAYERS];
};

// 게임 중
struct C2S_Input {		// UDP, 33ms마다
	PacketHeader header;
	uint32_t seq;
	uint8_t keys;		// InputKey 비트
	float aimAngle;
};
struct C2S_SelectWeapon {
	PacketHeader header;
	uint8_t cardIndex;	// 0 ~ 2
};
struct S2C_LevelUp {
	PacketHeader header;
	uint8_t choices[LEVELUP_CHOICES];	// WeaponType
};
struct S2C_BossAlert {
	PacketHeader header;
	uint8_t bossType;	// BossType
};
struct S2C_GameResult {
	PacketHeader header;
	uint8_t result;		// GameResult
};

// 모든 조각: PlayerState x2 -> uint16 적 수 -> EnemyState[]
// 0번 조각만 적 수 앞에: uint8 보스 수 + BossState[] -> uint16 탄막 수 + BulletState[] -> uint8 기믹 수 + GimmickState[]
struct S2C_SnapshotHead {
	PacketHeader header;
	uint32_t tick;
	uint8_t fragIndex;
	uint8_t fragCount;
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 3, "PacketHeader must be 3 bytes");
static_assert(sizeof(S2C_SnapshotHead) == 9, "SnapshotHead must be 9 bytes");

// 헤더 채우기: InitPacket(pkt, C2S_LOGIN);
template<class T>
inline void InitPacket(T& pkt, PacketType type) {
	pkt.header.size = static_cast<uint16_t>(sizeof(T));
	pkt.header.type = type;
}