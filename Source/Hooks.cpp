#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "HassleNet/Detail/Internal.h"
#include "HassleNet/SLikeNet/StringCompressor.h"

#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>
#include <windows.h>

namespace HassleNet::Detail {
    Addresses addresses;

    ReceiveFn origReceive = nullptr;
    SendBitStreamFn origSendBitStream = nullptr;
    SendRawFn origSendRaw = nullptr;
    DeallocateFn origDeallocatePacket = nullptr;
    SignalFn origRpcSignal = nullptr;
    RpcOnReceiveFn origRpcOnReceive = nullptr;

    namespace {
        constexpr std::uintptr_t kImageBase = 0x140000000ULL;

        constexpr std::uintptr_t kReceiveAddress = 0x1457244C0ULL;
        constexpr std::uintptr_t kSendBitStreamAddress = 0x145727800ULL;
        constexpr std::uintptr_t kSendRawAddress = 0x145727620ULL;
        constexpr std::uintptr_t kDeallocateAddress = 0x14571C8A0ULL;
        constexpr std::uintptr_t kRpcSignalAddress = 0x145733340ULL;
        constexpr std::uintptr_t kRpcOnReceiveAddress = 0x145730A50ULL;

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

        bool GetModuleRegion(std::uint8_t*& base, std::size_t& size) {
            base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
            size = 0;
            if (!base)
                return false;

            auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
                return false;

            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE)
                return false;

            size = nt->OptionalHeader.SizeOfImage;
            return size > 0;
        }

        void* Resolve(std::uint8_t* base, std::size_t imageSize, std::uintptr_t address) {
            if (!base || imageSize == 0)
                return nullptr;

            const std::uintptr_t rva = address >= kImageBase ? (address - kImageBase) : address;
            if (rva == 0 || rva >= imageSize)
                return nullptr;

            return base + rva;
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
                if (origDeallocatePacket)
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
            if (!bitStream || bitStream->GetNumberOfBytesUsed() == 0)
                return origSendBitStream(peer,
                                         bitStream,
                                         priority,
                                         reliability,
                                         orderingChannel,
                                         systemIdentifier,
                                         broadcast,
                                         forceReceiptNumber);
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
            SLikeNet::BitStream view(reinterpret_cast<const unsigned char*>(data),
                                     static_cast<unsigned int>(length),
                                     true);
            SLikeNet::PacketPriority mutablePriority = priority;
            SLikeNet::PacketReliability mutableReliability = reliability;
            char mutableChannel = orderingChannel;
            if (!DispatchSendPacket(&view, mutablePriority, mutableReliability, mutableChannel))
                return 0;

            const bool payloadChanged =
                view.GetNumberOfBytesUsed() != static_cast<SLikeNet::BitSize_t>(length) ||
                memcmp(view.GetData(), data, static_cast<std::size_t>(length)) != 0;
            const char* sendData =
                payloadChanged ? reinterpret_cast<const char*>(view.GetData()) : data;
            const int sendLength =
                payloadChanged ? static_cast<int>(view.GetNumberOfBytesUsed()) : length;

            return origSendRaw(peer,
                               sendData,
                               sendLength,
                               mutablePriority,
                               mutableReliability,
                               mutableChannel,
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
            if (rpc)
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
            if (rpc)
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
            out.Write(static_cast<unsigned char>(kRpcPluginId));
            out.Write(static_cast<unsigned char>(kRpc4Signal));
            char newName[16];
            snprintf(newName, sizeof(newName), "%d", mutableId);
            SLikeNet::StringCompressor::Instance()->EncodeString(newName, 0, &out, 0);
            out.AlignWriteToByteBoundary();
            if (payload.GetNumberOfBitsUsed() > 0)
                out.WriteBits(payload.GetData(), payload.GetNumberOfBitsUsed(), false);

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
        auto entry = reinterpret_cast<RpcOnReceiveFn>(addresses.rpcOnReceive);
        if (!rpc || !entry || !origRpcOnReceive || !packet)
            return false;

        lastRpcBlocked = false;
        const char result = entry(rpc, packet);
        return result != kRrStopProcessingAndDeallocate && !lastRpcBlocked;
    }

    bool InstallHooks() {
        std::uint8_t* base = nullptr;
        std::size_t imageSize = 0;
        if (!GetModuleRegion(base, imageSize))
            return false;

        addresses.receive = Resolve(base, imageSize, kReceiveAddress);
        addresses.sendBitStream = Resolve(base, imageSize, kSendBitStreamAddress);
        addresses.sendRaw = Resolve(base, imageSize, kSendRawAddress);
        addresses.deallocatePacket = Resolve(base, imageSize, kDeallocateAddress);
        addresses.rpcSignal = Resolve(base, imageSize, kRpcSignalAddress);
        addresses.rpcOnReceive = Resolve(base, imageSize, kRpcOnReceiveAddress);

        if (!addresses.receive || !addresses.sendBitStream || !addresses.sendRaw ||
            !addresses.deallocatePacket || !addresses.rpcSignal || !addresses.rpcOnReceive) {
            return false;
        }

        origDeallocatePacket = reinterpret_cast<DeallocateFn>(addresses.deallocatePacket);

        if (MH_Initialize() != MH_OK)
            return false;

        struct HookDesc {
            void* target;
            void* detour;
            void** original;
        } hooks[] = {
            {addresses.receive, (void*)&DetourReceive, (void**)&origReceive},
            {addresses.sendBitStream, (void*)&DetourSendBitStream, (void**)&origSendBitStream},
            {addresses.sendRaw, (void*)&DetourSendRaw, (void**)&origSendRaw},
            {addresses.rpcSignal, (void*)&DetourRpcSignal, (void**)&origRpcSignal},
            {addresses.rpcOnReceive, (void*)&DetourRpcOnReceive, (void**)&origRpcOnReceive},
        };

        for (const HookDesc& hook : hooks) {
            MH_STATUS status = MH_CreateHook(hook.target, hook.detour, hook.original);
            if (status != MH_OK) {
                RemoveHooks();
                return false;
            }
            createdHooks[createdHookCount++] = hook.target;
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
