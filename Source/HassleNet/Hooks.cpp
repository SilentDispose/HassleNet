#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "HassleNet/Detail/Internal.h"
#include "HassleNet/SLikeNet/StringCompressor.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include <windows.h>

using namespace HassleNet::Detail;

Addresses HassleNet::Detail::addresses;

ReceiveFn HassleNet::Detail::origReceive = nullptr;
SendBitStreamFn HassleNet::Detail::origSendBitStream = nullptr;
SendRawFn HassleNet::Detail::origSendRaw = nullptr;
DeallocateFn HassleNet::Detail::origDeallocatePacket = nullptr;
RpcOnReceiveFn HassleNet::Detail::origRpcOnReceive = nullptr;

static constexpr size_t kSendBitStreamSlot = 19;
static constexpr size_t kSendRawSlot = 20;
static constexpr size_t kReceiveSlot = 23;
static constexpr size_t kDeallocateSlot = 24;
static constexpr size_t kRpcOnReceiveSlot = 4;

static constexpr size_t kPeerOffset = 0x28;
static constexpr size_t kRpc4Offset = 0x30;

static constexpr unsigned char kRpcPluginId = SLikeNet::ID_RPC_PLUGIN;
static constexpr unsigned char kRpc4Signal = 2;
static constexpr char kRrStop = 0;

static std::atomic<void*> globalPeer = nullptr;
static std::atomic<void*> rpc4 = nullptr;

static std::mutex senderMutex;
static SLikeNet::SystemAddress lastSender = SLikeNet::UNASSIGNED_SYSTEM_ADDRESS();
static SLikeNet::RakNetGUID lastSenderGuid = SLikeNet::UNASSIGNED_RAKNET_GUID();

static std::mutex sendTargetMutex;
static SLikeNet::AddressOrGUID lastSendTarget(SLikeNet::UNASSIGNED_SYSTEM_ADDRESS());

static thread_local bool lastRpcBlocked = false;

static std::mutex installMutex;
static bool hooked = false;
static void* hookedPeer = nullptr;
static void* hookedRpc = nullptr;
static std::atomic<bool> resolverStop = false;
static std::thread resolverThread;

static void** VirtualSlotPtr(void* object, size_t index) {
    void* vtable = *reinterpret_cast<void**>(object);
    return reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(vtable) + index * sizeof(void*));
}

static void* CasSlot(void** slot, void* value, void* expected) {
    while (true) {
        void* observed = nullptr;
        bool faulted = false;
        __try {
            observed = InterlockedCompareExchangePointer(slot, value, expected);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            faulted = true;
        }
        if (!faulted)
            return observed;
        DWORD oldProtect = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
            return nullptr;
    }
}

static void* HookVirtualSlot(void* object, size_t index, void* value) {
    void** slot = VirtualSlotPtr(object, index);

    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(slot, &mbi, sizeof(mbi)))
        return nullptr;

    void* previous = *slot;
    while (previous) {
        void* observed = CasSlot(slot, value, previous);
        if (!observed)
            return nullptr;
        if (observed == previous)
            break;
        previous = observed;
    }
    if (!previous)
        return nullptr;

    DWORD oldProtect = 0;
    VirtualProtect(slot, sizeof(void*), mbi.Protect, &oldProtect);
    return previous;
}

static bool UnhookVirtualSlot(void* object, size_t index, void* mine, void* previous) {
    void** slot = VirtualSlotPtr(object, index);

    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(slot, &mbi, sizeof(mbi)))
        return false;

    const bool unlinked = CasSlot(slot, previous, mine) == mine;

    DWORD oldProtect = 0;
    VirtualProtect(slot, sizeof(void*), mbi.Protect, &oldProtect);
    return unlinked;
}

static bool ParseRpcPacket(const unsigned char* data,
                           unsigned int bytes,
                           int& id,
                           SLikeNet::BitStream& payload) {
    if (!data || bytes < 3 || data[0] != kRpcPluginId || data[1] != kRpc4Signal)
        return false;

    SLikeNet::BitStream in(data, bytes, false);
    in.SetReadOffset(16);

    char name[256] = {};
    if (!SLikeNet::StringCompressor::Instance()->DecodeString(name, sizeof(name), &in))
        return false;
    in.AlignReadToByteBoundary();

    id = ParseRpcId(name);
    if (id < 0)
        return false;

    auto bits = in.GetNumberOfUnreadBits();
    if (bits > 0)
        in.Read(&payload, bits);
    return true;
}

void HassleNet::Detail::WriteRpcPacket(int id,
                                       const SLikeNet::BitStream* payload,
                                       SLikeNet::BitStream& out) {
    out.Reset();
    out.Write(kRpcPluginId);
    out.Write(kRpc4Signal);

    char name[16];
    snprintf(name, sizeof(name), "%d", id);
    SLikeNet::StringCompressor::Instance()->EncodeString(name, 0, &out, 0);
    out.AlignWriteToByteBoundary();

    if (payload && payload->GetNumberOfBitsUsed() > 0)
        out.WriteBits(payload->GetData(), payload->GetNumberOfBitsUsed(), false);
}

static RpcSendResult ProcessOutgoingRpc(const SLikeNet::BitStream* bitStream,
                                        SLikeNet::BitStream& rebuilt,
                                        SLikeNet::PacketPriority& priority,
                                        SLikeNet::PacketReliability& reliability,
                                        char& orderingChannel) {
    int id = -1;
    SLikeNet::BitStream payload;
    if (!ParseRpcPacket(bitStream->GetData(), bitStream->GetNumberOfBytesUsed(), id, payload))
        return RpcSendResult::NotRpc;

    const auto beforeBits = payload.GetNumberOfBitsUsed();
    std::vector<unsigned char> before(payload.GetNumberOfBytesUsed());
    if (!before.empty())
        memcpy(before.data(), payload.GetData(), before.size());

    int mutableId = id;
    if (!DispatchSendRpc(mutableId, &payload, priority, reliability, orderingChannel))
        return RpcSendResult::Blocked;

    const bool idChanged = mutableId != id;
    const bool payloadChanged =
        payload.GetNumberOfBitsUsed() != beforeBits ||
        before.size() != payload.GetNumberOfBytesUsed() ||
        (!before.empty() && memcmp(before.data(), payload.GetData(), before.size()) != 0);

    if (!idChanged && !payloadChanged)
        return RpcSendResult::Unchanged;

    WriteRpcPacket(mutableId, &payload, rebuilt);
    return RpcSendResult::Changed;
}

static SLikeNet::Packet* DetourReceive(void* peer) {
    CapturePeer(peer);
    SLikeNet::Packet* packet = origReceive(peer);
    if (!packet)
        return nullptr;
    CaptureSender(packet->systemAddress, packet->guid);
    if (!DispatchReceivePacket(packet)) {
        if (origDeallocatePacket)
            origDeallocatePacket(peer, packet);
        return nullptr;
    }
    return packet;
}

static uint32_t DetourSendBitStream(void* peer,
                                    const SLikeNet::BitStream* bitStream,
                                    SLikeNet::PacketPriority priority,
                                    SLikeNet::PacketReliability reliability,
                                    char orderingChannel,
                                    SLikeNet::AddressOrGUID systemIdentifier,
                                    bool broadcast,
                                    uint32_t forceReceiptNumber) {
    CapturePeer(peer);
    if (!broadcast && !systemIdentifier.IsUndefined())
        CaptureSendTarget(systemIdentifier);

    if (!bitStream || bitStream->GetNumberOfBytesUsed() == 0)
        return origSendBitStream(peer,
                                 bitStream,
                                 priority,
                                 reliability,
                                 orderingChannel,
                                 systemIdentifier,
                                 broadcast,
                                 forceReceiptNumber);

    auto mutablePriority = priority;
    auto mutableReliability = reliability;
    char mutableChannel = orderingChannel;

    SLikeNet::BitStream rebuilt;
    const SLikeNet::BitStream* outgoing = bitStream;
    switch (ProcessOutgoingRpc(
        bitStream, rebuilt, mutablePriority, mutableReliability, mutableChannel)) {
        case RpcSendResult::Blocked:
            return 0;
        case RpcSendResult::Changed:
            outgoing = &rebuilt;
            break;
        default:
            break;
    }

    if (!DispatchSendPacket(const_cast<SLikeNet::BitStream*>(outgoing),
                            mutablePriority,
                            mutableReliability,
                            mutableChannel))
        return 0;

    return origSendBitStream(peer,
                             outgoing,
                             mutablePriority,
                             mutableReliability,
                             mutableChannel,
                             systemIdentifier,
                             broadcast,
                             forceReceiptNumber);
}

static uint32_t DetourSendRaw(void* peer,
                              const char* data,
                              const int length,
                              SLikeNet::PacketPriority priority,
                              SLikeNet::PacketReliability reliability,
                              char orderingChannel,
                              SLikeNet::AddressOrGUID systemIdentifier,
                              bool broadcast,
                              uint32_t forceReceiptNumber) {
    CapturePeer(peer);
    if (!broadcast && !systemIdentifier.IsUndefined())
        CaptureSendTarget(systemIdentifier);

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

    SLikeNet::BitStream view(reinterpret_cast<const unsigned char*>(data), length, true);
    auto mutablePriority = priority;
    auto mutableReliability = reliability;
    char mutableChannel = orderingChannel;
    if (!DispatchSendPacket(&view, mutablePriority, mutableReliability, mutableChannel))
        return 0;

    bool changed = view.GetNumberOfBytesUsed() != static_cast<unsigned>(length) ||
                   memcmp(view.GetData(), data, length) != 0;
    const char* sendData = changed ? reinterpret_cast<const char*>(view.GetData()) : data;
    int sendLength = changed ? static_cast<int>(view.GetNumberOfBytesUsed()) : length;

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

static char DetourRpcOnReceive(void* rpc, SLikeNet::Packet* packet) {
    CaptureRpc4(rpc);
    if (rpc)
        CapturePeer(*reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(rpc) + 8));

    int id = -1;
    SLikeNet::BitStream payload;
    if (!packet || !ParseRpcPacket(packet->data, packet->length, id, payload))
        return origRpcOnReceive(rpc, packet);

    CaptureSender(packet->systemAddress, packet->guid);

    const auto beforeBits = payload.GetNumberOfBitsUsed();
    std::vector<unsigned char> before(payload.GetNumberOfBytesUsed());
    if (!before.empty())
        memcpy(before.data(), payload.GetData(), before.size());

    int mutableId = id;
    if (!DispatchReceiveRpc(mutableId, &payload)) {
        lastRpcBlocked = true;
        return kRrStop;
    }

    const bool idChanged = mutableId != id;
    const bool payloadChanged =
        payload.GetNumberOfBitsUsed() != beforeBits ||
        before.size() != payload.GetNumberOfBytesUsed() ||
        (!before.empty() && memcmp(before.data(), payload.GetData(), before.size()) != 0);

    if (!idChanged && !payloadChanged)
        return origRpcOnReceive(rpc, packet);

    SLikeNet::BitStream out;
    WriteRpcPacket(mutableId, &payload, out);

    SLikeNet::Packet rebuilt = *packet;
    rebuilt.data = out.GetData();
    rebuilt.length = out.GetNumberOfBytesUsed();
    rebuilt.bitSize = out.GetNumberOfBitsUsed();
    rebuilt.deleteData = false;

    return origRpcOnReceive(rpc, &rebuilt);
}

static bool InstallHooks() {
    origDeallocatePacket = reinterpret_cast<DeallocateFn>(addresses.deallocatePacket);

    origReceive = reinterpret_cast<ReceiveFn>(
        HookVirtualSlot(hookedPeer, kReceiveSlot, reinterpret_cast<void*>(&DetourReceive)));
    if (!origReceive)
        return false;

    origSendBitStream = reinterpret_cast<SendBitStreamFn>(
        HookVirtualSlot(hookedPeer, kSendBitStreamSlot, reinterpret_cast<void*>(&DetourSendBitStream)));
    if (!origSendBitStream)
        return false;

    origSendRaw = reinterpret_cast<SendRawFn>(
        HookVirtualSlot(hookedPeer, kSendRawSlot, reinterpret_cast<void*>(&DetourSendRaw)));
    if (!origSendRaw)
        return false;

    origRpcOnReceive = reinterpret_cast<RpcOnReceiveFn>(
        HookVirtualSlot(hookedRpc, kRpcOnReceiveSlot, reinterpret_cast<void*>(&DetourRpcOnReceive)));
    if (!origRpcOnReceive)
        return false;

    return true;
}

void* HassleNet::Detail::PeerRaw() {
    return globalPeer.load(std::memory_order_acquire);
}

void* HassleNet::Detail::Rpc4Raw() {
    return rpc4.load(std::memory_order_acquire);
}

void HassleNet::Detail::CapturePeer(void* peer) {
    if (peer)
        globalPeer.store(peer, std::memory_order_release);
}

void HassleNet::Detail::CaptureRpc4(void* rpc) {
    if (rpc)
        rpc4.store(rpc, std::memory_order_release);
}

void HassleNet::Detail::CaptureSender(const SLikeNet::SystemAddress& address,
                                      const SLikeNet::RakNetGUID& guid) {
    std::lock_guard lock(senderMutex);
    lastSender = address;
    lastSenderGuid = guid;
}

void HassleNet::Detail::CaptureSendTarget(const SLikeNet::AddressOrGUID& target) {
    std::lock_guard lock(sendTargetMutex);
    lastSendTarget = target;
}

SLikeNet::AddressOrGUID HassleNet::Detail::GetSendTarget() {
    std::lock_guard lock(sendTargetMutex);
    return lastSendTarget;
}

bool HassleNet::Detail::GetLastSender(SLikeNet::SystemAddress& address,
                                      SLikeNet::RakNetGUID& guid) {
    std::lock_guard lock(senderMutex);
    address = lastSender;
    guid = lastSenderGuid;
    return !lastSender.IsUndefined();
}

bool HassleNet::Detail::EmulateRpcPacket(SLikeNet::Packet* packet) {
    void* rpc = rpc4.load(std::memory_order_acquire);
    if (!rpc || !packet)
        return false;

    auto entry = reinterpret_cast<RpcOnReceiveFn>(*VirtualSlotPtr(rpc, kRpcOnReceiveSlot));
    if (!entry)
        return false;

    lastRpcBlocked = false;
    char result = entry(rpc, packet);
    return result != kRrStop && !lastRpcBlocked;
}

void HassleNet::Detail::RemoveHooks() {
    bool unlinkedReceive = true;
    bool unlinkedSendBitStream = true;
    bool unlinkedSendRaw = true;
    bool unlinkedRpc = true;

    if (hookedPeer) {
        unlinkedReceive = UnhookVirtualSlot(
            hookedPeer, kReceiveSlot, reinterpret_cast<void*>(&DetourReceive), origReceive);
        unlinkedSendBitStream = UnhookVirtualSlot(hookedPeer,
                                                  kSendBitStreamSlot,
                                                  reinterpret_cast<void*>(&DetourSendBitStream),
                                                  origSendBitStream);
        unlinkedSendRaw = UnhookVirtualSlot(
            hookedPeer, kSendRawSlot, reinterpret_cast<void*>(&DetourSendRaw), origSendRaw);
    }
    if (hookedRpc) {
        unlinkedRpc = UnhookVirtualSlot(
            hookedRpc, kRpcOnReceiveSlot, reinterpret_cast<void*>(&DetourRpcOnReceive), origRpcOnReceive);
    }

    if (unlinkedReceive)
        origReceive = nullptr;
    if (unlinkedSendBitStream)
        origSendBitStream = nullptr;
    if (unlinkedSendRaw)
        origSendRaw = nullptr;
    if (unlinkedRpc)
        origRpcOnReceive = nullptr;

    hooked = false;
}

InstallResult HassleNet::Detail::TryInstallHooks() {
    std::lock_guard lock(installMutex);
    if (hooked)
        return InstallResult::Installed;

    ModuleInfo module;
    uint64_t classStorage = 0;
    if (!LocateMetadata(module, classStorage))
        return InstallResult::Failed;

    uint64_t uclass = 0;
    if (!TryReadQword(reinterpret_cast<const void*>(classStorage), uclass) || !uclass)
        return InstallResult::NotReady;
    if (!ValidateObjectVtable(module, reinterpret_cast<void*>(uclass)))
        return InstallResult::NotReady;

    void* instance = FindClassInstance(module, reinterpret_cast<void*>(uclass));
    if (!instance)
        return InstallResult::NotReady;

    uint64_t peer = 0;
    uint64_t rpc = 0;
    if (!TryReadQword(static_cast<unsigned char*>(instance) + kPeerOffset, peer) ||
        !TryReadQword(static_cast<unsigned char*>(instance) + kRpc4Offset, rpc) || !peer || !rpc)
        return InstallResult::NotReady;

    void* peerObj = reinterpret_cast<void*>(peer);
    void* rpcObj = reinterpret_cast<void*>(rpc);

    auto inRdata = [&](void* p) {
        return reinterpret_cast<const unsigned char*>(p) >= module.rdataBegin &&
               reinterpret_cast<const unsigned char*>(p) < module.rdataEnd;
    };
    if (!inRdata(*reinterpret_cast<void**>(peerObj)) || !inRdata(*reinterpret_cast<void**>(rpcObj)))
        return InstallResult::NotReady;

    addresses.receive = *VirtualSlotPtr(peerObj, kReceiveSlot);
    addresses.sendBitStream = *VirtualSlotPtr(peerObj, kSendBitStreamSlot);
    addresses.sendRaw = *VirtualSlotPtr(peerObj, kSendRawSlot);
    addresses.deallocatePacket = *VirtualSlotPtr(peerObj, kDeallocateSlot);
    addresses.rpcOnReceive = *VirtualSlotPtr(rpcObj, kRpcOnReceiveSlot);

    if (!addresses.receive || !addresses.sendBitStream || !addresses.sendRaw ||
        !addresses.deallocatePacket || !addresses.rpcOnReceive)
        return InstallResult::NotReady;

    hookedPeer = reinterpret_cast<void*>(peer);
    hookedRpc = reinterpret_cast<void*>(rpc);

    if (!InstallHooks())
        return InstallResult::Failed;

    hooked = true;
    return InstallResult::Installed;
}

void HassleNet::Detail::StartDeferredInstall() {
    if (resolverThread.joinable())
        return;

    resolverStop = false;
    resolverThread = std::thread([] {
        while (!resolverStop) {
            if (TryInstallHooks() != InstallResult::NotReady)
                return;
            for (int i = 0; i < 10; ++i) {
                if (resolverStop)
                    return;
                Sleep(10);
            }
        }
    });
}

void HassleNet::Detail::StopDeferredInstall() {
    resolverStop = true;
    if (resolverThread.joinable())
        resolverThread.join();
}
