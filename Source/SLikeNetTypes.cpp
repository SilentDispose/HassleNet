#include "HassleNet/SLikeNet/Types.h"

#include <cstdio>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace SLikeNet {
    SystemAddress::SystemAddress()
        : debugPort(0)
        , systemIndex((SystemIndex)-1) {
        memset(&address, 0, sizeof(address));
        address.addr4.sin_family = AF_INET;
    }

    SystemAddress::SystemAddress(const char* ip, unsigned short port)
        : debugPort(port)
        , systemIndex((SystemIndex)-1) {
        memset(&address, 0, sizeof(address));
        address.addr4.sin_family = AF_INET;
        address.addr4.sin_port = htons(port);
        inet_pton(AF_INET, ip, &address.addr4.sin_addr);
    }

    bool SystemAddress::EqualsExcludingPort(const SystemAddress& o) const {
        return address.addr4.sin_addr == o.address.addr4.sin_addr;
    }

    unsigned short SystemAddress::GetPort() const {
        return ntohs(address.addr4.sin_port);
    }

    uint32_t SystemAddress::GetIPv4() const {
        return ntohl(address.addr4.sin_addr);
    }

    bool SystemAddress::IsUndefined() const {
        return address.addr4.sin_addr == 0 && address.addr4.sin_port == 0 &&
               systemIndex == (SystemIndex)-1;
    }

    const char* SystemAddress::ToString(char* dest, size_t destSize, bool writePort) const {
        char ip[64] = {0};
        inet_ntop(AF_INET, &address.addr4.sin_addr, ip, sizeof(ip));
        if (writePort)
            snprintf(dest, destSize, "%s|%u", ip, GetPort());
        else
            snprintf(dest, destSize, "%s", ip);
        return dest;
    }

    const char* SystemAddress::ToString(bool writePort) const {
        static thread_local char buffer[80];
        return ToString(buffer, sizeof(buffer), writePort);
    }

    RakNetGUID::RakNetGUID()
        : g((uint64_t)-1)
        , systemIndex((SystemIndex)-1) {}

    RakNetGUID::RakNetGUID(uint64_t value)
        : g(value)
        , systemIndex((SystemIndex)-1) {}

    const char* RakNetGUID::ToString(char* dest, size_t destSize) const {
        snprintf(dest, destSize, "%016llx", (unsigned long long)g);
        return dest;
    }

    void AddressOrGUID::SetUndefined() {
        rakNetGuid = UNASSIGNED_RAKNET_GUID();
        systemAddress = UNASSIGNED_SYSTEM_ADDRESS();
    }

    void AddressOrGUID::SetUndefinedGuid() {
        rakNetGuid.g = (uint64_t)-1;
        rakNetGuid.systemIndex = (SystemIndex)-1;
    }

    void AddressOrGUID::SetUndefinedAddress() {
        systemAddress = UNASSIGNED_SYSTEM_ADDRESS();
    }

    const SystemAddress& UNASSIGNED_SYSTEM_ADDRESS() {
        static const SystemAddress value;
        return value;
    }

    const RakNetGUID& UNASSIGNED_RAKNET_GUID() {
        static const RakNetGUID value;
        return value;
    }
}
