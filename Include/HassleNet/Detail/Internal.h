#pragma once

#include "HassleNet/HassleNet.h"

namespace HassleNet::Detail {
    struct Addresses {
        void* receive = nullptr;
        void* sendBitStream = nullptr;
        void* sendRaw = nullptr;
        void* deallocatePacket = nullptr;
        void* rpcSignal = nullptr;
        void* rpcOnReceive = nullptr;
    };

    extern Addresses addresses;

    using ReceiveFn = SLikeNet::Packet* (*)(void*);
    using SendBitStreamFn = uint32_t (*)(void*,
                                         const SLikeNet::BitStream*,
                                         SLikeNet::PacketPriority,
                                         SLikeNet::PacketReliability,
                                         char,
                                         SLikeNet::AddressOrGUID,
                                         bool,
                                         uint32_t);
    using SendRawFn = uint32_t (*)(void*,
                                   const char*,
                                   int,
                                   SLikeNet::PacketPriority,
                                   SLikeNet::PacketReliability,
                                   char,
                                   SLikeNet::AddressOrGUID,
                                   bool,
                                   uint32_t);
    using DeallocateFn = void (*)(void*, SLikeNet::Packet*);
    using SignalFn = void (*)(void*,
                              const char*,
                              SLikeNet::BitStream*,
                              SLikeNet::PacketPriority,
                              SLikeNet::PacketReliability,
                              char,
                              SLikeNet::AddressOrGUID,
                              bool,
                              bool);
    using RpcOnReceiveFn = char (*)(void*, SLikeNet::Packet*);

    extern ReceiveFn origReceive;
    extern SendBitStreamFn origSendBitStream;
    extern SendRawFn origSendRaw;
    extern DeallocateFn origDeallocatePacket;
    extern SignalFn origRpcSignal;
    extern RpcOnReceiveFn origRpcOnReceive;

    bool InstallHooks();
    void RemoveHooks();

    void CaptureSender(const SLikeNet::SystemAddress& address, const SLikeNet::RakNetGUID& guid);
    bool GetLastSender(SLikeNet::SystemAddress& address, SLikeNet::RakNetGUID& guid);

    bool EmulateRpcPacket(SLikeNet::Packet* packet);

    void* PeerRaw();
    void* Rpc4Raw();

    void CapturePeer(void* peer);
    void CaptureRpc4(void* rpc4);

    bool DispatchReceivePacket(SLikeNet::Packet* packet);
    bool DispatchSendPacket(SLikeNet::BitStream* bitStream,
                            SLikeNet::PacketPriority& priority,
                            SLikeNet::PacketReliability& reliability,
                            char& orderingChannel);
    int ParseRpcId(const char* name);
    bool DispatchReceiveRpc(int& id, SLikeNet::BitStream* payload);
    bool DispatchSendRpc(int& id,
                         SLikeNet::BitStream* payload,
                         SLikeNet::PacketPriority& priority,
                         SLikeNet::PacketReliability& reliability,
                         char& orderingChannel);
}
