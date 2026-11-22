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
#include <kthook/kthook.hpp>
#include <memory>
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

using ReceiveHook = kthook::kthook_simple<ReceiveFn, kthook::kFreezeThreads>;
using SendBitStreamHook = kthook::kthook_simple<SendBitStreamFn, kthook::kFreezeThreads>;
using SendRawHook = kthook::kthook_simple<SendRawFn, kthook::kFreezeThreads>;
using RpcReceiveHook = kthook::kthook_simple<RpcOnReceiveFn, kthook::kFreezeThreads>;

static std::unique_ptr<ReceiveHook> receiveHook;
static std::unique_ptr<SendBitStreamHook> sendBitStreamHook;
static std::unique_ptr<SendRawHook> sendRawHook;
static std::unique_ptr<RpcReceiveHook> rpcOnReceiveHook;

static std::mutex installMutex;
static bool hooked = false;
static std::atomic<bool> resolverStop = false;
static std::thread resolverThread;

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

static SLikeNet::Packet* DetourReceive(ReceiveFn original, void* peer) {
    CapturePeer(peer);
    SLikeNet::Packet* packet = original(peer);
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

static uint32_t DetourSendBitStream(SendBitStreamFn original,
                                    void* peer,
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
        return original(peer,
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

    return original(peer,
                    outgoing,
                    mutablePriority,
                    mutableReliability,
                    mutableChannel,
                    systemIdentifier,
                    broadcast,
                    forceReceiptNumber);
}

static uint32_t DetourSendRaw(SendRawFn original,
                              void* peer,
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
        return original(peer,
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

    return original(peer,
                    sendData,
                    sendLength,
                    mutablePriority,
                    mutableReliability,
                    mutableChannel,
                    systemIdentifier,
                    broadcast,
                    forceReceiptNumber);
}

static char DetourRpcOnReceive(RpcOnReceiveFn original, void* rpc, SLikeNet::Packet* packet) {
    CaptureRpc4(rpc);
    if (rpc)
        CapturePeer(*reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(rpc) + 8));

    int id = -1;
    SLikeNet::BitStream payload;
    if (!packet || !ParseRpcPacket(packet->data, packet->length, id, payload))
        return original(rpc, packet);

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
        return original(rpc, packet);

    SLikeNet::BitStream out;
    WriteRpcPacket(mutableId, &payload, out);

    SLikeNet::Packet rebuilt = *packet;
    rebuilt.data = out.GetData();
    rebuilt.length = out.GetNumberOfBytesUsed();
    rebuilt.bitSize = out.GetNumberOfBitsUsed();
    rebuilt.deleteData = false;

    return original(rpc, &rebuilt);
}

static bool InstallHooks() {
    origDeallocatePacket = reinterpret_cast<DeallocateFn>(addresses.deallocatePacket);

    receiveHook = std::make_unique<ReceiveHook>(addresses.receive);
    receiveHook->set_cb([](const auto& hook, auto&... args) {
        return DetourReceive(hook.get_trampoline(), args...);
    });

    sendBitStreamHook = std::make_unique<SendBitStreamHook>(addresses.sendBitStream);
    sendBitStreamHook->set_cb([](const auto& hook, auto&... args) {
        return DetourSendBitStream(hook.get_trampoline(), args...);
    });

    sendRawHook = std::make_unique<SendRawHook>(addresses.sendRaw);
    sendRawHook->set_cb([](const auto& hook, auto&... args) {
        return DetourSendRaw(hook.get_trampoline(), args...);
    });

    rpcOnReceiveHook = std::make_unique<RpcReceiveHook>(addresses.rpcOnReceive);
    rpcOnReceiveHook->set_cb([](const auto& hook, auto&... args) {
        return DetourRpcOnReceive(hook.get_trampoline(), args...);
    });

    if (!receiveHook->install() || !sendBitStreamHook->install() || !sendRawHook->install() ||
        !rpcOnReceiveHook->install()) {
        receiveHook.release();
        sendBitStreamHook.release();
        sendRawHook.release();
        rpcOnReceiveHook.release();
        return false;
    }

    origReceive = receiveHook->get_trampoline();
    origSendBitStream = sendBitStreamHook->get_trampoline();
    origSendRaw = sendRawHook->get_trampoline();
    origRpcOnReceive = rpcOnReceiveHook->get_trampoline();
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
    auto entry = reinterpret_cast<RpcOnReceiveFn>(addresses.rpcOnReceive);
    if (!rpc || !entry || !origRpcOnReceive || !packet)
        return false;

    lastRpcBlocked = false;
    char result = entry(rpc, packet);
    return result != kRrStop && !lastRpcBlocked;
}

void HassleNet::Detail::RemoveHooks() {
    rpcOnReceiveHook.release();
    sendRawHook.release();
    sendBitStreamHook.release();
    receiveHook.release();

    origReceive = nullptr;
    origSendBitStream = nullptr;
    origSendRaw = nullptr;
    origDeallocatePacket = nullptr;
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

    addresses.receive = ReadVirtualSlot(module, reinterpret_cast<void*>(peer), kReceiveSlot);
    addresses.sendBitStream =
        ReadVirtualSlot(module, reinterpret_cast<void*>(peer), kSendBitStreamSlot);
    addresses.sendRaw = ReadVirtualSlot(module, reinterpret_cast<void*>(peer), kSendRawSlot);
    addresses.deallocatePacket =
        ReadVirtualSlot(module, reinterpret_cast<void*>(peer), kDeallocateSlot);
    addresses.rpcOnReceive =
        ReadVirtualSlot(module, reinterpret_cast<void*>(rpc), kRpcOnReceiveSlot);

    if (!addresses.receive || !addresses.sendBitStream || !addresses.sendRaw ||
        !addresses.deallocatePacket || !addresses.rpcOnReceive)
        return InstallResult::NotReady;

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
