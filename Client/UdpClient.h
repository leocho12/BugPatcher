#pragma once
#include <cstdint>

class UdpClient {
public:
	bool Start(const char* serverIp, uint8_t playerId, uint32_t token);
	void SendInput(uint8_t keys, float aimAngle);
	void Stop();
};