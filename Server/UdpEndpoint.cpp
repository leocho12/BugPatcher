#include "UdpEndpoint.h"

bool UdpEndpoint::Init(uint16_t) { return true; }
SOCKET UdpEndpoint::GetSocket() const { return INVALID_SOCKET; }
void UdpEndpoint::OnReadable(EventQueue<RecvEvent>&) {}
void UdpEndpoint::SendTo(int, const char*, int) {}
bool UdpEndpoint::IsRegistered(int)const { return false; }