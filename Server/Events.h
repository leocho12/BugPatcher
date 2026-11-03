#pragma once
#include <cstdint>
#include <vector>
#include "Protocol.h"

// 메인 -> 월드
enum class RecvKind : uint8_t { Packet, Disconnect };
struct RecvEvent {
	RecvKind kind;
	uint8_t playerId;
	std::vector<char> data;		
};

// 월드 -> 메인
constexpr uint8_t TARGET_ALL = 0xFF;	
struct SendEvent {
	uint8_t target;
	bool udp;					
	std::vector<char> data;
};

// 고정 크기 패킷 구조체를 바이트 복사본으로
template<class T>
std::vector<char> ToBytes(const T& pkt) {
	const char* p = reinterpret_cast<const char*>(&pkt);
	return std::vector<char>(p, p + pkt.header.size);
}

struct GameStartInfo {
	uint8_t mapId;
	char nickname[MAX_PLAYERS][NICKNAME_LEN];
};