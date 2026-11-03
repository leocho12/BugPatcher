#include "Server.h"

bool Server::Init() { return true; }
void Server::Run() {}
void Server::Shutdown() {}
void Server::ProcessPacket(Session&, const PacketHeader*) {}

void Server::OnAccept() {}
void Server::OnLogin(Session&, const C2S_Login&) {}
void Server::OnMapSelect(Session&, const C2S_MapSelect&) {}
void Server::SendLoginOk(Session&) {}
void Server::SendUdpOk(Session&) {}
void Server::CheckGameStart() {}
void Server::FlushSendQueue() {}
void Server::Disconnect(Session&) {}