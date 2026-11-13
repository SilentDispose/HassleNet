#include "HassleNet/Detail/Internal.h"
#include "HassleNet/Detail/PatternScan.h"
#include "HassleNet/SLikeNet/StringCompressor.h"

#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace HassleNet::Detail {
    Addresses addresses;

    ReceiveFn origReceive = nullptr;
    SendBitStreamFn origSendBitStream = nullptr;
    SendRawFn origSendRaw = nullptr;
    DeallocateFn origDeallocatePacket = nullptr;
    SignalFn origRpcSignal = nullptr;
    RpcOnReceiveFn origRpcOnReceive = nullptr;

    namespace {
        constexpr const char* kReceivePattern = "40 53 55 56 57 41 54 41 56 41 57 48 83 EC 20 48 "
                                                "8B 01 48 8B D9 FF 50 78 84 C0 0F 84 ?? ?? ?? ??";
        constexpr const char* kSendBitStreamPattern =
            "48 89 5C 24 20 55 57 41 54 41 56 41 57 48 81 EC 90 00 00 00 48 8B 05 ?? ?? ?? ?? 48 "
            "33 C4 48 89 84 24 ?? ?? ?? ??";
        constexpr const char* kSendRawPattern =
            "40 53 55 57 41 54 41 56 41 57 48 81 EC 98 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 "
            "89 84 24 ?? ?? ?? ??";
        constexpr const char* kDeallocatePattern =
            "48 85 D2 0F 84 ?? ?? ?? ?? 48 89 74 24 18 57 48 83 EC 20 80 7A 38 ?? 48 8B F2 48 8D "
            "15 ?? ?? ?? ??";
        constexpr const char* kRpcSignalPattern = "40 55 53 57 41 54 41 55 41 56 48 8D AC 24 ?? ?? "
                                                  "?? ?? 48 81 EC 48 03 00 00 48 8B 05 ?? ?? ?? ??";
        constexpr const char* kRpcOnReceivePattern =
            "40 55 53 56 57 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? 48 81 EC C0 05 00 00 48 8B "
            "05 ?? ?? ?? ??";

        constexpr unsigned char kRpcPluginId = SLikeNet::ID_RPC_PLUGIN;
        constexpr unsigned char kRpc4Signal = 2;
        constexpr char kRrStopProcessingAndDeallocate = 0;

        std::atomic<void*> globalPeer = nullptr;
        std::atomic<void*> rpc4 = nullptr;

        std::mutex senderMutex;
        SLikeNet::SystemAddress lastSender = SLikeNet::UNASSIGNED_SYSTEM_ADDRESS();
        SLikeNet::RakNetGUID lastSenderGuid = SLikeNet::UNASSIGNED_RAKNET_GUID();
        thread_local bool lastRpcBlocked = false;
        void* createdHooks[8] = {};
        int createdHookCount = 0;

        void* Resolve(const char* name, const char* pattern) {
            (void)name;
            return FindPattern(pattern);
        }

        SLikeNet::Packet* DetourReceive(void* peer) {
            CapturePeer(peer);
            for (int guard = 0; guard < 1024; ++guard) {
                SLikeNet::Packet* packet = origReceive(peer);
                if (!packet)
                    return nullptr;
                CaptureSender(packet->systemAddress, packet->guid);
                if (DispatchReceivePacket(packet))
                    return packet;
                origDeallocatePacket(peer, packet);
            }
            return origReceive(peer);
        }

        uint32_t DetourSendBitStream(void* peer,
                                     const SLikeNet::BitStream* bitStream,
                                     SLikeNet::PacketPriority priority,
                                     SLikeNet::PacketReliability reliability,
                                     char orderingChannel,
                                     SLikeNet::AddressOrGUID systemIdentifier,
                                     bool broadcast,
                                     uint32_t forceReceiptNumber) {
            CapturePeer(peer);
            SLikeNet::PacketPriority mutablePriority = priority;
            SLikeNet::PacketReliability mutableReliability = reliability;
            char mutableChannel = orderingChannel;
            if (!DispatchSendPacket(const_cast<SLikeNet::BitStream*>(bitStream),
                                    mutablePriority,
                                    mutableReliability,
                                    mutableChannel))
                return 0;
            return origSendBitStream(peer,
                                     bitStream,
                                     mutablePriority,
                                     mutableReliability,
                                     mutableChannel,
                                     systemIdentifier,
                                     broadcast,
                                     forceReceiptNumber);
        }

        uint32_t DetourSendRaw(void* peer,
                               const char* data,
                               const int length,
                               SLikeNet::PacketPriority priority,
                               SLikeNet::PacketReliability reliability,
                               char orderingChannel,
                               SLikeNet::AddressOrGUID systemIdentifier,
                               bool broadcast,
                               uint32_t forceReceiptNumber) {
            CapturePeer(peer);
            if (!data || length <= 0)
                return origSendRaw(peer,
                                   data,
                                   length,
                                   priority,
                                   reliability,
                                   orderingChannel,
                                   systemIdentifier,
                                   broadcast,
                                   forceReceiptNumber);
            SLikeNet::BitStream view(
                reinterpret_cast<const unsigned char*>(data), (unsigned int)length, true);
            SLikeNet::PacketPriority mutablePriority = priority;
            SLikeNet::PacketReliability mutableReliability = reliability;
            char mutableChannel = orderingChannel;
            if (!DispatchSendPacket(&view, mutablePriority, mutableReliability, mutableChannel))
                return 0;
            return origSendRaw(peer,
                               data,
                               length,
                               priority,
                               reliability,
                               orderingChannel,
                               systemIdentifier,
                               broadcast,
                               forceReceiptNumber);
        }

        void DetourRpcSignal(void* rpc,
                             const char* sharedIdentifier,
                             SLikeNet::BitStream* bitStream,
                             SLikeNet::PacketPriority priority,
                             SLikeNet::PacketReliability reliability,
                             char orderingChannel,
                             SLikeNet::AddressOrGUID systemIdentifier,
                             bool broadcast,
                             bool invokeLocal) {
            CaptureRpc4(rpc);
            CapturePeer(*reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(rpc) + 8));

            if (!sharedIdentifier || !origRpcSignal)
                return;

            const int id = ParseRpcId(sharedIdentifier);
            int mutableId = id;
            SLikeNet::PacketPriority mutablePriority = priority;
            SLikeNet::PacketReliability mutableReliability = reliability;
            char mutableChannel = orderingChannel;

            if (!DispatchSendRpc(
                    mutableId, bitStream, mutablePriority, mutableReliability, mutableChannel))
                return;

            char newName[16];
            const char* outName = sharedIdentifier;
            if (mutableId != id) {
                snprintf(newName, sizeof(newName), "%d", mutableId);
                outName = newName;
            }

            origRpcSignal(rpc,
                          outName,
                          bitStream,
                          mutablePriority,
                          mutableReliability,
                          mutableChannel,
                          systemIdentifier,
                          broadcast,
                          invokeLocal);
        }

        char DetourRpcOnReceive(void* rpc, SLikeNet::Packet* packet) {
            CaptureRpc4(rpc);
            CapturePeer(*reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(rpc) + 8));

            if (!packet || !packet->data || packet->length < 3 || packet->data[0] != kRpcPluginId ||
                packet->data[1] != kRpc4Signal)
                return origRpcOnReceive(rpc, packet);

            CaptureSender(packet->systemAddress, packet->guid);

            SLikeNet::BitStream incoming(packet->data, packet->length, false);
            incoming.SetReadOffset(16);

            char name[256] = {0};
            if (!SLikeNet::StringCompressor::Instance()->DecodeString(
                    name, sizeof(name), &incoming))
                return origRpcOnReceive(rpc, packet);
            incoming.AlignReadToByteBoundary();

            int id = ParseRpcId(name);
            int mutableId = id;

            SLikeNet::BitStream payload;
            const SLikeNet::BitSize_t payloadBits = incoming.GetNumberOfUnreadBits();
            if (payloadBits > 0)
                incoming.Read(&payload, payloadBits);

            const SLikeNet::BitSize_t beforeBits = payload.GetNumberOfBitsUsed();
            std::vector<unsigned char> before(payload.GetNumberOfBytesUsed());
            if (!before.empty())
                memcpy(before.data(), payload.GetData(), before.size());

            if (!DispatchReceiveRpc(mutableId, &payload)) {
                lastRpcBlocked = true;
                return kRrStopProcessingAndDeallocate;
            }

            const bool idChanged = mutableId != id;
            const bool payloadChanged =
                payload.GetNumberOfBitsUsed() != beforeBits ||
                before.size() != payload.GetNumberOfBytesUsed() ||
                (!before.empty() && memcmp(before.data(), payload.GetData(), before.size()) != 0);

            if (!idChanged && !payloadChanged)
                return origRpcOnReceive(rpc, packet);

            SLikeNet::BitStream out;
            out.Write((unsigned char)kRpcPluginId);
            out.Write((unsigned char)kRpc4Signal);
            char newName[16];
            snprintf(newName, sizeof(newName), "%d", mutableId);
            SLikeNet::StringCompressor::Instance()->EncodeString(newName, 0, &out, 0);
            out.AlignWriteToByteBoundary();
            if (payload.GetNumberOfBitsUsed() > 0)
                out.Write(&payload, payload.GetNumberOfBitsUsed());

            SLikeNet::Packet rebuilt = *packet;
            rebuilt.data = out.GetData();
            rebuilt.length = out.GetNumberOfBytesUsed();
            rebuilt.bitSize = out.GetNumberOfBitsUsed();
            rebuilt.deleteData = false;

            return origRpcOnReceive(rpc, &rebuilt);
        }
    }

    void* PeerRaw() {
        return globalPeer.load(std::memory_order_acquire);
    }
    void* Rpc4Raw() {
        return rpc4.load(std::memory_order_acquire);
    }

    void CapturePeer(void* peer) {
        if (peer)
            globalPeer.store(peer, std::memory_order_release);
    }

    void CaptureRpc4(void* rpc) {
        if (rpc)
            rpc4.store(rpc, std::memory_order_release);
    }

    void CaptureSender(const SLikeNet::SystemAddress& address, const SLikeNet::RakNetGUID& guid) {
        std::lock_guard lock(senderMutex);
        lastSender = address;
        lastSenderGuid = guid;
    }

    bool GetLastSender(SLikeNet::SystemAddress& address, SLikeNet::RakNetGUID& guid) {
        std::lock_guard lock(senderMutex);
        address = lastSender;
        guid = lastSenderGuid;
        return !lastSender.IsUndefined();
    }

    bool EmulateRpcPacket(SLikeNet::Packet* packet) {
        void* rpc = rpc4.load(std::memory_order_acquire);
        if (!rpc || !origRpcOnReceive || !packet)
            return false;

        lastRpcBlocked = false;
        DetourRpcOnReceive(rpc, packet);
        return !lastRpcBlocked;
    }

    bool InstallHooks() {
        addresses.receive = Resolve("RakPeer::Receive", kReceivePattern);
        addresses.sendBitStream = Resolve("RakPeer::Send(BitStream*)", kSendBitStreamPattern);
        addresses.sendRaw = Resolve("RakPeer::Send(char*,int)", kSendRawPattern);
        addresses.deallocatePacket = Resolve("RakPeer::DeallocatePacket", kDeallocatePattern);
        addresses.rpcSignal = Resolve("RPC4::Signal", kRpcSignalPattern);
        addresses.rpcOnReceive = Resolve("RPC4::OnReceive", kRpcOnReceivePattern);

        if (!addresses.receive || !addresses.sendBitStream || !addresses.sendRaw ||
            !addresses.deallocatePacket || !addresses.rpcSignal || !addresses.rpcOnReceive) {
            return false;
        }

        if (MH_Initialize() != MH_OK)
            return false;

        struct HookDesc {
            void* target;
            void* detour;
            void** original;
            const char* name;
        } hooks[] = {
            {addresses.receive, (void*)&DetourReceive, (void**)&origReceive, "Receive"},
            {addresses.sendBitStream,
             (void*)&DetourSendBitStream,
             (void**)&origSendBitStream,
             "Send(BitStream)"},
            {addresses.sendRaw, (void*)&DetourSendRaw, (void**)&origSendRaw, "Send(char*)"},
            {addresses.rpcSignal, (void*)&DetourRpcSignal, (void**)&origRpcSignal, "RPC4::Signal"},
            {addresses.rpcOnReceive,
             (void*)&DetourRpcOnReceive,
             (void**)&origRpcOnReceive,
             "RPC4::OnReceive"},
        };

        for (const HookDesc& hook : hooks) {
            MH_STATUS status = MH_CreateHook(hook.target, hook.detour, hook.original);
            if (status != MH_OK) {
                RemoveHooks();
                return false;
            }
        }

        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
            RemoveHooks();
            return false;
        }

        return true;
    }

    void RemoveHooks() {
        if (createdHookCount > 0) {
            MH_DisableHook(MH_ALL_HOOKS);
            for (int i = 0; i < createdHookCount; ++i)
                MH_RemoveHook(createdHooks[i]);
            createdHookCount = 0;
        }
        MH_Uninitialize();
        origReceive = nullptr;
        origSendBitStream = nullptr;
        origSendRaw = nullptr;
        origDeallocatePacket = nullptr;
        origRpcSignal = nullptr;
        origRpcOnReceive = nullptr;
    }
}
