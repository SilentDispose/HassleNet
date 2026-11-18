#pragma once

#include "HassleNet/HassleNet.h"

#include <cstdint>

namespace HassleNet::Detail {
    struct Addresses {
        void* receive = nullptr;
        void* sendBitStream = nullptr;
        void* sendRaw = nullptr;
        void* deallocatePacket = nullptr;
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
    using RpcOnReceiveFn = char (*)(void*, SLikeNet::Packet*);

    extern ReceiveFn origReceive;
    extern SendBitStreamFn origSendBitStream;
    extern SendRawFn origSendRaw;
    extern DeallocateFn origDeallocatePacket;
    extern RpcOnReceiveFn origRpcOnReceive;

    enum class InstallResult { Installed, NotReady, Failed };
    enum class RpcSendResult { NotRpc, Unchanged, Changed, Blocked };

    InstallResult TryInstallHooks();
    void StartDeferredInstall();
    void StopDeferredInstall();
    void RemoveHooks();

    void CaptureSender(const SLikeNet::SystemAddress& address, const SLikeNet::RakNetGUID& guid);
    bool GetLastSender(SLikeNet::SystemAddress& address, SLikeNet::RakNetGUID& guid);

    void CaptureSendTarget(const SLikeNet::AddressOrGUID& target);
    SLikeNet::AddressOrGUID GetSendTarget();

    bool EmulateRpcPacket(SLikeNet::Packet* packet);

    void* PeerRaw();
    void* Rpc4Raw();

    void CapturePeer(void* peer);
    void CaptureRpc4(void* rpc4);
}
