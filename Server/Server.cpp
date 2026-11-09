#include "Server.h"
#include <cstdio>
#include <WS2tcpip.h>

constexpr int POLL_TIMEOUT_MS = 5;

bool Server::Init() {
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		printf("WSAStartup 실패\n");
		return false;
	}

	listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSock == INVALID_SOCKET) {
		printf("socket 실패: %d\n", WSAGetLastError());
		return false;
	}

	BOOL reuse = TRUE;
	setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(SERVER_TCP_PORT);
	if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
		printf("bind 실패: %d\n", WSAGetLastError());
		return false;
	}
	if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
		printf("listen 실패: %d\n", WSAGetLastError());
		return false;
	}

	u_long nonBlocking = 1;
	ioctlsocket(listenSock, FIONBIO, &nonBlocking);

	if (!udp.Init(SERVER_UDP_PORT)) {
		printf("UDP 초기화 실패\n");
		return false;
	}

	gameStartEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (gameStartEvent == nullptr) return false;

	world.Init(&recvQ, &sendQ, gameStartEvent, &startInfo);
	worldThread = CreateThread(nullptr, 0, World::ThreadMain, &world, 0, nullptr);
	if (worldThread == nullptr) return false;

	printf("서버 시작: TCP %d, UDP %d\n", SERVER_TCP_PORT, SERVER_UDP_PORT);
	return true;
}
void Server::Run() {
	while (true) {
		WSAPOLLFD fds[2 + MAX_PLAYERS];
		Session* owner[2 + MAX_PLAYERS]{};
		int n = 0;
		int udpIndex = -1;

		fds[n].fd = listenSock;
		fds[n].events = POLLRDNORM;
		fds[n].revents = 0;
		++n;

		if (udp.GetSocket() != INVALID_SOCKET) {
			udpIndex = n;
			fds[n].fd = udp.GetSocket();
			fds[n].events = POLLRDNORM;
			fds[n].revents = 0;
			++n;
		}

		for (Session& s : sessions) {
			if (s.sock == INVALID_SOCKET) continue;
			fds[n].fd = s.sock;
			fds[n].events = POLLRDNORM | (s.HasPendingSend() ? POLLWRNORM : 0);
			fds[n].revents = 0;
			owner[n] = &s;
			++n;
		}

		int ret = WSAPoll(fds, n, POLL_TIMEOUT_MS);
		if (ret == SOCKET_ERROR) {
			printf("WSAPoll 실패: %d\n", WSAGetLastError());
			break;
		}
		for (int i = 0; ret > 0 && i < n; ++i) {
			if (fds[i].revents == 0) continue;

			if (i == 0) {
				OnAccept();
			}
			else if (i == udpIndex) {
				udp.OnReadable(recvQ);
			}
			else {
				Session& s = *owner[i];
				if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
					Disconnect(s);
					continue;
				}
				if (fds[i].revents & POLLRDNORM) {
					char buf[4096];
					int len = recv(s.sock, buf, sizeof(buf), 0);
					if (len == 0) {						
						Disconnect(s);
						continue;
					}
					if (len == SOCKET_ERROR) {
						if (WSAGetLastError() != WSAEWOULDBLOCK) {
							Disconnect(s);
							continue;
						}
					}
					else if (!s.OnRecv(*this, buf, len)) {	
						Disconnect(s);
						continue;
					}
				}				if ((fds[i].revents & POLLWRNORM) && !s.FlushSend()) {
					Disconnect(s);
				}
			}
		}
		FlushSendQueue();
		CheckGameStart();
	}
}
void Server::Shutdown() {
	for (Session& s : sessions) {
		if (s.sock != INVALID_SOCKET) closesocket(s.sock);
		s.sock = INVALID_SOCKET;
	}
	if (listenSock != INVALID_SOCKET) closesocket(listenSock);
	listenSock = INVALID_SOCKET;

	if (worldThread) {
		world.Stop();
		SetEvent(gameStartEvent);	
		WaitForSingleObject(worldThread, INFINITE);
		CloseHandle(worldThread);
		worldThread = nullptr;
	}	
	if (gameStartEvent) { CloseHandle(gameStartEvent); gameStartEvent = nullptr; }
	WSACleanup();
}

void Server::ProcessPacket(Session& s, const PacketHeader* pkt) {
	printf("P%d 패킷 수신: type=%d size=%d\n", s.playerId, pkt->type, pkt->size);
}

void Server::OnAccept() {
	while (true) {
		sockaddr_in clientAddr{};
		int addrLen = sizeof(clientAddr);
		SOCKET cs = accept(listenSock, (sockaddr*)&clientAddr, &addrLen);
		if (cs == INVALID_SOCKET) {
			if (WSAGetLastError() != WSAEWOULDBLOCK)
				printf("accept 실패: %d\n", WSAGetLastError());
			return;
		}
		Session* slot = nullptr;
		if (!gameStarted) {
			for (Session& s : sessions) {
				if (s.sock == INVALID_SOCKET) { slot = &s; break; }
			}
		}
		if (slot == nullptr) {
			printf("접속 거절: 빈 자리 없음\n");
			closesocket(cs);
			continue;
		}

		u_long nonBlocking = 1;
		ioctlsocket(cs, FIONBIO, &nonBlocking);
		BOOL noDelay = TRUE;	
		setsockopt(cs, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));

		slot->Reset();
		slot->sock = cs;
		slot->playerId = (int)(slot - sessions);	

		char ip[INET_ADDRSTRLEN]{};
		inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
		printf("접속: P%d %s:%d\n", slot->playerId, ip, ntohs(clientAddr.sin_port));
	}
}
void Server::OnLogin(Session&, const C2S_Login&) {}
void Server::OnMapSelect(Session&, const C2S_MapSelect&) {}
void Server::SendLoginOk(Session&) {}
void Server::SendUdpOk(Session&) {}
void Server::CheckGameStart() {}
void Server::FlushSendQueue() {
	sendQ.SwapOut(sendBuf);
	for (const SendEvent& e : sendBuf) {
		for (int i = 0; i < MAX_PLAYERS; ++i) {
			if (e.target != TARGET_ALL && e.target != i) continue;
			if (e.udp)
				udp.SendTo(i, e.data.data(), (int)e.data.size());	// 등록 안 된 플레이어는 SendTo가 무시
			else if (sessions[i].sock != INVALID_SOCKET)
				sessions[i].AppendSend(e.data.data(), (int)e.data.size());	// 다음 WSAPoll에서 POLLWRNORM으로 보냄
		}
	}
}
void Server::Disconnect(Session& s) {
	if (s.sock == INVALID_SOCKET) return;
	printf("접속 끊김: P%d\n", s.playerId);
	closesocket(s.sock);
	if (gameStarted)
		recvQ.Push(RecvEvent{ RecvKind::Disconnect, (uint8_t)s.playerId, {} });
	s.Reset();
}