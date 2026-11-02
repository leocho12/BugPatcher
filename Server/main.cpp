#include <WinSock2.h>
#pragma comment(lib,"ws2_32.lib")
int main() {
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	WSACleanup();
	return 0;
}