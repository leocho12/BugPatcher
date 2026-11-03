#include "NetworkClient.h"

bool NetworkClient::Connect(const char*, HWND) { return true; }
void NetworkClient::Close() {}
void NetworkClient::SendLogin(const char*) {}
void NetworkClient::SendMapSelect(uint8_t) {}
void NetworkClient::SendSelectWeapon(uint8_t) {}

DWORD WINAPI NetworkClient::RecvThread(LPVOID) { return 0; }
bool NetworkClient::Send(const void*, int) { return true; }
void NetworkClient::HandlePacket(const PacketHeader*) {}