#include "Server.h"
#pragma comment(lib, "ws2_32.lib")

int main() {
	Server server;
	if (!server.Init()) return 1;
	server.Run();
	server.Shutdown();
	return 0;
}