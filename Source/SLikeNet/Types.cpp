#include "HassleNet/SLikeNet/Types.h"

#include <cstdio>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

SLikeNet::SystemAddress::SystemAddress()
    : debugPort(0)
    , systemIndex((SystemIndex)-1) {
    memset(&address, 0, sizeof(address));
    address.addr4.sin_family = AF_INET;
}

SLikeNet::SystemAddress::SystemAddress(const char* ip, unsigned short port)
    : debugPort(port)
    , systemIndex((SystemIndex)-1) {
    memset(&address, 0, sizeof(address));
    address.addr4.sin_family = AF_INET;
    address.addr4.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.addr4.sin_addr);
}

bool SLikeNet::SystemAddress::EqualsExcludingPort(const SystemAddress& o) const {
    return address.addr4.sin_addr == o.address.addr4.sin_addr;
}

unsigned short SLikeNet::SystemAddress::GetPort() const {
    return ntohs(address.addr4.sin_port);
}

uint32_t SLikeNet::SystemAddress::GetIPv4() const {
    return ntohl(address.addr4.sin_addr);
}

bool SLikeNet::SystemAddress::IsUndefined() const {
    return address.addr4.sin_addr == 0 && address.addr4.sin_port == 0 &&
           systemIndex == (SystemIndex)-1;
}

const char* SLikeNet::SystemAddress::ToString(char* dest, size_t destSize, bool writePort) const {
    char ip[64] = {0};
    inet_ntop(AF_INET, &address.addr4.sin_addr, ip, sizeof(ip));
    if (writePort)
        snprintf(dest, destSize, "%s|%u", ip, GetPort());
    else
        snprintf(dest, destSize, "%s", ip);
    return dest;
}

const char* SLikeNet::SystemAddress::ToString(bool writePort) const {
    static thread_local char buffer[80];
    return ToString(buffer, sizeof(buffer), writePort);
}

SLikeNet::RakNetGUID::RakNetGUID()
    : g((uint64_t)-1)
    , systemIndex((SystemIndex)-1) {}

SLikeNet::RakNetGUID::RakNetGUID(uint64_t value)
    : g(value)
    , systemIndex((SystemIndex)-1) {}

const char* SLikeNet::RakNetGUID::ToString(char* dest, size_t destSize) const {
    snprintf(dest, destSize, "%016llx", (unsigned long long)g);
    return dest;
}

void SLikeNet::AddressOrGUID::SetUndefined() {
    rakNetGuid = UNASSIGNED_RAKNET_GUID();
    systemAddress = UNASSIGNED_SYSTEM_ADDRESS();
}

void SLikeNet::AddressOrGUID::SetUndefinedGuid() {
    rakNetGuid.g = (uint64_t)-1;
    rakNetGuid.systemIndex = (SystemIndex)-1;
}

void SLikeNet::AddressOrGUID::SetUndefinedAddress() {
    systemAddress = UNASSIGNED_SYSTEM_ADDRESS();
}

const SLikeNet::SystemAddress& SLikeNet::UNASSIGNED_SYSTEM_ADDRESS() {
    static const SystemAddress value;
    return value;
}

const SLikeNet::RakNetGUID& SLikeNet::UNASSIGNED_RAKNET_GUID() {
    static const RakNetGUID value;
    return value;
}
