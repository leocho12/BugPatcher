#include "Session.h"
#include "Server.h"

bool Session::OnRecv(Server&, const char*, int) { return true; }
void Session::AppendSend(const void*, int) {}
bool Session::FlushSend() { return true; }
void Session::Reset() {}