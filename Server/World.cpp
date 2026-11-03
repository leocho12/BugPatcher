#include "World.h"

void World::Init(EventQueue<RecvEvent>* rq, EventQueue<SendEvent>* sq,
	HANDLE ev, const GameStartInfo* info) {
	recvQ = rq;
	sendQ = sq;
	startEvent = ev;
	startInfo = info;
}
DWORD WINAPI World::ThreadMain(LPVOID) { return 0; }
void World::OnInput(int, const C2S_Input&) {}
void World::OnSelectWeapon(int,const C2S_SelectWeapon&){}
void World::OnDisconnect(int) {}