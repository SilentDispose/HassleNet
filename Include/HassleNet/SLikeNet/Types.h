#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace SLikeNet {
    typedef unsigned int BitSize_t;
    typedef unsigned short SystemIndex;
    typedef uint32_t Time;
    typedef uint32_t TimeMS;
    typedef uint64_t NetworkID;

    enum PacketPriority {
        IMMEDIATE_PRIORITY,
        HIGH_PRIORITY,
        MEDIUM_PRIORITY,
        LOW_PRIORITY,
        NUMBER_OF_PRIORITIES
    };

    enum PacketReliability {
        UNRELIABLE,
        UNRELIABLE_SEQUENCED,
        RELIABLE,
        RELIABLE_ORDERED,
        RELIABLE_SEQUENCED,
        UNRELIABLE_WITH_ACK_RECEIPT,
        RELIABLE_WITH_ACK_RECEIPT,
        RELIABLE_ORDERED_WITH_ACK_RECEIPT,
        NUMBER_OF_RELIABILITIES
    };

    struct RakNetGUID;
    class BitStream;
    struct Packet;

    struct IPv4Address {
        unsigned short sin_family;
        unsigned short sin_port;
        uint32_t sin_addr;
        unsigned char sin_zero[8];
    };
    static_assert(sizeof(IPv4Address) == 16, "IPv4Address ABI mismatch");

    struct SystemAddress {
        union {
            IPv4Address addr4;
        } address;
        unsigned short debugPort;
        SystemIndex systemIndex;

        SystemAddress();
        SystemAddress(const char* ip, unsigned short port);

        bool EqualsExcludingPort(const SystemAddress& o) const;
        bool operator==(const SystemAddress& o) const {
            return address.addr4.sin_addr == o.address.addr4.sin_addr &&
                   address.addr4.sin_port == o.address.addr4.sin_port &&
                   systemIndex == o.systemIndex;
        }
        bool operator!=(const SystemAddress& o) const {
            return !(*this == o);
        }

        unsigned short GetPort() const;
        uint32_t GetIPv4() const;
        bool IsUndefined() const;
        const char* ToString(char* dest, size_t destSize, bool writePort = true) const;
        const char* ToString(bool writePort = true) const;
    };

    struct RakNetGUID {
        uint64_t g;
        SystemIndex systemIndex;

        RakNetGUID();
        explicit RakNetGUID(uint64_t value);

        bool operator==(const RakNetGUID& o) const {
            return g == o.g;
        }
        bool operator!=(const RakNetGUID& o) const {
            return g != o.g;
        }
        bool operator<(const RakNetGUID& o) const {
            return g < o.g;
        }
        bool IsUndefined() const {
            return g == (uint64_t)-1;
        }
        const char* ToString(char* dest, size_t destSize) const;
    };

    struct AddressOrGUID {
        RakNetGUID rakNetGuid;
        SystemAddress systemAddress;

        AddressOrGUID() {}
        AddressOrGUID(const AddressOrGUID& o)
            : rakNetGuid(o.rakNetGuid)
            , systemAddress(o.systemAddress) {}
        explicit AddressOrGUID(const SystemAddress& o)
            : systemAddress(o) {
            SetUndefinedGuid();
        }
        explicit AddressOrGUID(const RakNetGUID& o)
            : rakNetGuid(o) {
            SetUndefinedAddress();
        }

        AddressOrGUID& operator=(const SystemAddress& o) {
            SetUndefinedGuid();
            systemAddress = o;
            return *this;
        }
        AddressOrGUID& operator=(const RakNetGUID& o) {
            rakNetGuid = o;
            SetUndefinedAddress();
            return *this;
        }

        bool IsUndefined() const {
            return rakNetGuid.IsUndefined() && systemAddress.IsUndefined();
        }
        void SetUndefined();

    private:
        void SetUndefinedGuid();
        void SetUndefinedAddress();
    };

    struct Packet {
        SystemAddress systemAddress;
        RakNetGUID guid;
        unsigned int length;
        BitSize_t bitSize;
        unsigned char* data;
        bool deleteData;
        bool wasGeneratedLocally;
    };

    static_assert(sizeof(SystemAddress) == 20, "SystemAddress ABI mismatch");
    static_assert(sizeof(RakNetGUID) == 16, "RakNetGUID ABI mismatch");
    static_assert(sizeof(AddressOrGUID) == 40, "AddressOrGUID ABI mismatch");
    static_assert(sizeof(Packet) == 64, "Packet ABI mismatch");
    static_assert(offsetof(Packet, guid) == 0x18, "Packet::guid offset");
    static_assert(offsetof(Packet, length) == 0x28, "Packet::length offset");
    static_assert(offsetof(Packet, data) == 0x30, "Packet::data offset");

    const SystemAddress& UNASSIGNED_SYSTEM_ADDRESS();
    const RakNetGUID& UNASSIGNED_RAKNET_GUID();
}
