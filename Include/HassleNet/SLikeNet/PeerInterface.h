#pragma once

#include "BitStream.h"
#include "Types.h"

#include <cstdint>

namespace SLikeNet {
    struct SocketDescriptor;
    struct PublicKey;
    class RakNetSocket2;
    class RakNetStatistics;
    struct RNS2RecvStruct;
    class PluginInterface2;

    namespace DataStructures {
        template <class T> class List;
    }

    enum StartupResult {
        RAKNET_STARTED,
        RAKNET_ALREADY_STARTED,
        INVALID_SOCKET_DESCRIPTORS,
        INVALID_MAX_CONNECTIONS,
        SOCKET_FAMILY_NOT_SUPPORTED,
        SOCKET_PORT_ALREADY_IN_USE,
        SOCKET_FAILED_TO_BIND,
        SOCKET_FAILED_TEST_SEND,
        PORT_CANNOT_BE_ZERO,
        FAILED_TO_CREATE_NETWORK_THREAD,
        COULD_NOT_GENERATE_GUID,
        STARTUP_OTHER_FAILURE
    };
    enum ConnectionAttemptResult {
        CONNECTION_ATTEMPT_STARTED,
        INVALID_PARAMETER,
        CANNOT_RESOLVE_DOMAIN_NAME,
        ALREADY_CONNECTED_TO_ENDPOINT,
        CONNECTION_ATTEMPT_ALREADY_IN_PROGRESS,
        SECURITY_INITIALIZATION_FAILED
    };
    enum ConnectionState {
        IS_NOT_CONNECTED,
        IS_CONNECTING,
        IS_CONNECTED,
        IS_DISCONNECTING,
        IS_SILENTLY_DISCONNECTING,
        IS_DISCONNECTED,
        IS_BANNED
    };

    class RakPeerInterface {
    public:
        virtual ~RakPeerInterface() {}
        virtual StartupResult Startup(unsigned int maxConnections,
                                      SocketDescriptor* socketDescriptors,
                                      unsigned socketDescriptorCount,
                                      int threadPriority = -99999) = 0;
        virtual bool InitializeSecurity(const char* publicKey,
                                        const char* privateKey,
                                        bool bRequireClientKey = false) = 0;
        virtual void DisableSecurity(void) = 0;
        virtual void AddToSecurityExceptionList(const char* ip) = 0;
        virtual void RemoveFromSecurityExceptionList(const char* ip) = 0;
        virtual bool IsInSecurityExceptionList(const char* ip) = 0;
        virtual void SetMaximumIncomingConnections(unsigned short numberAllowed) = 0;
        virtual unsigned int GetMaximumIncomingConnections(void) const = 0;
        virtual unsigned short NumberOfConnections(void) const = 0;
        virtual void SetIncomingPassword(const char* passwordData, int passwordDataLength) = 0;
        virtual void GetIncomingPassword(char* passwordData, int* passwordDataLength) = 0;
        virtual ConnectionAttemptResult Connect(const char* host,
                                                unsigned short remotePort,
                                                const char* passwordData,
                                                int passwordDataLength,
                                                PublicKey* publicKey = 0,
                                                unsigned connectionSocketIndex = 0,
                                                unsigned sendConnectionAttemptCount = 12,
                                                unsigned timeBetweenSendConnectionAttemptsMS = 500,
                                                TimeMS timeoutTime = 0) = 0;
        virtual ConnectionAttemptResult
        ConnectWithSocket(const char* host,
                          unsigned short remotePort,
                          const char* passwordData,
                          int passwordDataLength,
                          RakNetSocket2* socket,
                          PublicKey* publicKey = 0,
                          unsigned sendConnectionAttemptCount = 12,
                          unsigned timeBetweenSendConnectionAttemptsMS = 500,
                          TimeMS timeoutTime = 0) = 0;
        virtual void Shutdown(unsigned int blockDuration,
                              unsigned char orderingChannel = 0,
                              PacketPriority disconnectionNotificationPriority = LOW_PRIORITY) = 0;
        virtual bool IsActive(void) const = 0;
        virtual bool GetConnectionList(SystemAddress* remoteSystems,
                                       unsigned short* numberOfSystems) const = 0;
        virtual uint32_t GetNextSendReceipt(void) = 0;
        virtual uint32_t IncrementNextSendReceipt(void) = 0;
        virtual uint32_t Send(const BitStream* bitStream,
                              PacketPriority priority,
                              PacketReliability reliability,
                              char orderingChannel,
                              const AddressOrGUID systemIdentifier,
                              bool broadcast,
                              uint32_t forceReceiptNumber = 0) = 0;
        virtual uint32_t Send(const char* data,
                              const int length,
                              PacketPriority priority,
                              PacketReliability reliability,
                              char orderingChannel,
                              const AddressOrGUID systemIdentifier,
                              bool broadcast,
                              uint32_t forceReceiptNumber = 0) = 0;
        virtual void SendLoopback(const char* data, const int length) = 0;
        virtual uint32_t SendList(const char** data,
                                  const int* lengths,
                                  const int numParameters,
                                  PacketPriority priority,
                                  PacketReliability reliability,
                                  char orderingChannel,
                                  const AddressOrGUID systemIdentifier,
                                  bool broadcast,
                                  uint32_t forceReceiptNumber = 0) = 0;
        virtual Packet* Receive(void) = 0;
        virtual void DeallocatePacket(Packet* packet) = 0;
        virtual unsigned int GetMaximumNumberOfPeers(void) const = 0;
        virtual void
        CloseConnection(const AddressOrGUID target,
                        bool sendDisconnectionNotification,
                        unsigned char orderingChannel = 0,
                        PacketPriority disconnectionNotificationPriority = LOW_PRIORITY) = 0;
        virtual ConnectionState GetConnectionState(const AddressOrGUID systemIdentifier) = 0;
        virtual void CancelConnectionAttempt(const SystemAddress target) = 0;
        virtual int GetIndexFromSystemAddress(const SystemAddress systemAddress) const = 0;
        virtual SystemAddress GetSystemAddressFromIndex(unsigned int index) = 0;
        virtual RakNetGUID GetGUIDFromIndex(unsigned int index) = 0;
        virtual void GetSystemList(DataStructures::List<SystemAddress>& addresses,
                                   DataStructures::List<RakNetGUID>& guids) const = 0;
        virtual void AddToBanList(const char* IP, TimeMS milliseconds = 0) = 0;
        virtual void RemoveFromBanList(const char* IP) = 0;
        virtual void ClearBanList(void) = 0;
        virtual bool IsBanned(const char* IP) = 0;
        virtual void SetLimitIPConnectionFrequency(bool b) = 0;
        virtual void Ping(const SystemAddress target) = 0;
        virtual bool Ping(const char* host,
                          unsigned short remotePort,
                          bool onlyReplyOnAcceptingConnections,
                          unsigned connectionSocketIndex = 0) = 0;
        virtual int GetAveragePing(const AddressOrGUID systemIdentifier) = 0;
        virtual int GetLastPing(const AddressOrGUID systemIdentifier) const = 0;
        virtual int GetLowestPing(const AddressOrGUID systemIdentifier) const = 0;
        virtual void SetOccasionalPing(bool doPing) = 0;
        virtual Time GetClockDifferential(const AddressOrGUID systemIdentifier) = 0;
        virtual void SetOfflinePingResponse(const char* data, const unsigned int length) = 0;
        virtual void GetOfflinePingResponse(char** data, unsigned int* length) = 0;
        virtual SystemAddress
        GetInternalID(const SystemAddress systemAddress = UNASSIGNED_SYSTEM_ADDRESS(),
                      const int index = 0) const = 0;
        virtual void SetInternalID(SystemAddress systemAddress, int index = 0) = 0;
        virtual SystemAddress GetExternalID(const SystemAddress target) const = 0;
        virtual const RakNetGUID GetMyGUID(void) const = 0;
        virtual SystemAddress GetMyBoundAddress(const int socketIndex = 0) = 0;
        static uint64_t Get64BitUniqueRandomNumber(void);
        virtual const RakNetGUID& GetGuidFromSystemAddress(const SystemAddress input) const = 0;
        virtual SystemAddress GetSystemAddressFromGuid(const RakNetGUID input) const = 0;
        virtual bool GetClientPublicKeyFromSystemAddress(const SystemAddress input,
                                                         char* client_public_key) const = 0;
        virtual void SetTimeoutTime(TimeMS timeMS, const SystemAddress target) = 0;
        virtual TimeMS GetTimeoutTime(const SystemAddress target) = 0;
        virtual int GetMTUSize(const SystemAddress target) const = 0;
        virtual unsigned GetNumberOfAddresses(void) = 0;
        virtual const char* GetLocalIP(unsigned int index) = 0;
        virtual bool IsLocalIP(const char* ip) = 0;
        virtual void AllowConnectionResponseIPMigration(bool allow) = 0;
        virtual bool AdvertiseSystem(const char* host,
                                     unsigned short remotePort,
                                     const char* data,
                                     int dataLength,
                                     unsigned connectionSocketIndex = 0) = 0;
        virtual void SetSplitMessageProgressInterval(int interval) = 0;
        virtual int GetSplitMessageProgressInterval(void) const = 0;
        virtual void SetUnreliableTimeout(TimeMS timeoutMS) = 0;
        virtual void SendTTL(const char* host,
                             unsigned short remotePort,
                             int ttl,
                             unsigned connectionSocketIndex = 0) = 0;
        virtual void AttachPlugin(PluginInterface2* plugin) = 0;
        virtual void DetachPlugin(PluginInterface2* messageHandler) = 0;
        virtual void PushBackPacket(Packet* packet, bool pushAtHead) = 0;
        virtual void ChangeSystemAddress(RakNetGUID guid, const SystemAddress& systemAddress) = 0;
        virtual Packet* AllocatePacket(unsigned dataSize) = 0;
        virtual RakNetSocket2* GetSocket(const SystemAddress target) = 0;
        virtual void GetSockets(DataStructures::List<RakNetSocket2*>& sockets) = 0;
        virtual void ReleaseSockets(DataStructures::List<RakNetSocket2*>& sockets) = 0;
        virtual void WriteOutOfBandHeader(BitStream* bitStream) = 0;
        virtual void SetUserUpdateThread(void (*_userUpdateThreadPtr)(RakPeerInterface*, void*),
                                         void* _userUpdateThreadData) = 0;
        virtual void
        SetIncomingDatagramEventHandler(bool (*_incomingDatagramEventHandler)(RNS2RecvStruct*)) = 0;
        virtual void ApplyNetworkSimulator(float packetloss,
                                           unsigned short minExtraPing,
                                           unsigned short extraPingVariance) = 0;
        virtual void SetPerConnectionOutgoingBandwidthLimit(unsigned maxBitsPerSecond) = 0;
        virtual bool IsNetworkSimulatorActive(void) = 0;
        virtual RakNetStatistics* GetStatistics(const SystemAddress systemAddress,
                                                RakNetStatistics* rns = 0) = 0;
        virtual bool GetStatistics(const unsigned int index, RakNetStatistics* rns) = 0;
        virtual void GetStatisticsList(DataStructures::List<SystemAddress>& addresses,
                                       DataStructures::List<RakNetGUID>& guids,
                                       DataStructures::List<RakNetStatistics>& statistics) = 0;
        virtual unsigned int GetReceiveBufferSize(void) = 0;
        virtual bool RunUpdateCycle(BitStream& updateBitStream) = 0;
        virtual bool SendOutOfBand(const char* host,
                                   unsigned short remotePort,
                                   const char* data,
                                   BitSize_t dataLength,
                                   unsigned connectionSocketIndex = 0) = 0;
    };
}
