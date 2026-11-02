#include "World.h"

DWORD WINAPI World::ThreadMain(LPVOID) { return 0; }
void World::OnInput(int, const C2S_Input&) {}
void World::OnSelectWeapon(int,const C2S_SelectWeapon&){}
void World::OnDisconnect(int) {}