#pragma once
#include <WinSock2.h>
#include <Windows.h>
struct C2S_Input;
struct C2S_SelectWeapon;

class World {
public:
	static DWORD WINAPI ThreadMain(LPVOID self);
	void OnInput(int playerId, const C2S_Input& pkt);
	void OnSelectWeapon(int playerId, const C2S_SelectWeapon& pkt);
	void OnDisconnect(int playerId);
};