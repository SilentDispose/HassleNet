#include "HassleNet/HassleNet.h"

#include "HassleNet/Detail/Internal.h"
#include "HassleNet/SLikeNet/StringCompressor.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

using namespace HassleNet::Detail;

static std::mutex lifecycleMutex;
static std::atomic<bool> initialized = false;

static SLikeNet::AddressOrGUID ServerTarget() {
    return GetSendTarget();
}

bool HassleNet::Initialize() {
    if (initialized.load(std::memory_order_acquire))
        return true;

    std::lock_guard lock(lifecycleMutex);
    if (initialized.load(std::memory_order_relaxed))
        return true;

    const InstallResult result = TryInstallHooks();
    if (result == InstallResult::Failed)
        return false;

    if (result == InstallResult::NotReady)
        StartDeferredInstall();

    initialized.store(true, std::memory_order_release);
    return true;
}

void HassleNet::Destroy() {
    std::lock_guard lock(lifecycleMutex);
    if (!initialized.exchange(false, std::memory_order_acq_rel))
        return;

    StopDeferredInstall();
    RemoveHooks();
    ClearHandlers();
}

bool HassleNet::IsInitialized() {
    return initialized.load(std::memory_order_acquire);
}

void HassleNet::OnReceiveRpc(ReceiveRpcHandler handler) {
    if (!handler)
        return;
    MutateConfig([&](Config& cfg) { cfg.receiveRpc.push_back(std::move(handler)); });
}

void HassleNet::OnSendRpc(SendRpcHandler handler) {
    if (!handler)
        return;
    MutateConfig([&](Config& cfg) { cfg.sendRpc.push_back(std::move(handler)); });
}

void HassleNet::OnReceivePacket(ReceivePacketHandler handler) {
    if (!handler)
        return;
    MutateConfig([&](Config& cfg) { cfg.receivePacket.push_back(std::move(handler)); });
}

void HassleNet::OnSendPacket(SendPacketHandler handler) {
    if (!handler)
        return;
    MutateConfig([&](Config& cfg) { cfg.sendPacket.push_back(std::move(handler)); });
}

void HassleNet::ClearHandlers() {
    MutateConfig([](Config& cfg) {
        cfg.receiveRpc.clear();
        cfg.sendRpc.clear();
        cfg.receivePacket.clear();
        cfg.sendPacket.clear();
    });
}

void HassleNet::BlockRpc(int id, bool block) {
    MutateConfig([&](Config& cfg) {
        if (block)
            cfg.blockedRpc.insert(id);
        else
            cfg.blockedRpc.erase(id);
    });
}

void HassleNet::BlockPacketId(unsigned char id, bool block) {
    MutateConfig([&](Config& cfg) {
        if (block)
            cfg.blockedPacketIds.insert(id);
        else
            cfg.blockedPacketIds.erase(id);
    });
}

bool HassleNet::IsRpcBlocked(int id) {
    return SnapshotConfig()->blockedRpc.count(id) != 0;
}

bool HassleNet::IsPacketIdBlocked(unsigned char id) {
    return SnapshotConfig()->blockedPacketIds.count(id) != 0;
}

bool HassleNet::IsReady() {
    return PeerRaw() != nullptr;
}

SLikeNet::RakPeerInterface* HassleNet::GetPeer() {
    return reinterpret_cast<SLikeNet::RakPeerInterface*>(PeerRaw());
}

void* HassleNet::GetRpc4() {
    return Rpc4Raw();
}

bool HassleNet::SendRpc(int id,
                        SLikeNet::BitStream* bs,
                        SLikeNet::PacketPriority priority,
                        SLikeNet::PacketReliability reliability,
                        char orderingChannel) {
    if (IsRpcBlocked(id))
        return false;

    void* peer = PeerRaw();
    if (!peer || !origSendBitStream)
        return false;

    SLikeNet::BitStream out;
    WriteRpcPacket(id, bs, out);

    SLikeNet::AddressOrGUID target = ServerTarget();
    origSendBitStream(
        peer, &out, priority, reliability, orderingChannel, target, target.IsUndefined(), 0);
    return true;
}

bool HassleNet::SendPacket(SLikeNet::BitStream* bs,
                           SLikeNet::PacketPriority priority,
                           SLikeNet::PacketReliability reliability,
                           char orderingChannel) {
    void* peer = PeerRaw();
    if (!peer || !bs || !origSendBitStream)
        return false;

    SLikeNet::AddressOrGUID target = ServerTarget();
    origSendBitStream(
        peer, bs, priority, reliability, orderingChannel, target, target.IsUndefined(), 0);
    return true;
}

bool HassleNet::SendPacketRaw(const void* data,
                              int length,
                              SLikeNet::PacketPriority priority,
                              SLikeNet::PacketReliability reliability,
                              char orderingChannel) {
    void* peer = PeerRaw();
    if (!peer || !data || length <= 0 || !origSendRaw)
        return false;

    SLikeNet::AddressOrGUID target = ServerTarget();
    origSendRaw(peer,
                reinterpret_cast<const char*>(data),
                length,
                priority,
                reliability,
                orderingChannel,
                target,
                target.IsUndefined(),
                0);
    return true;
}

bool HassleNet::EmulateRpc(int id, SLikeNet::BitStream& payload) {
    void* rpc = Rpc4Raw();
    if (!rpc || !origRpcOnReceive)
        return false;

    SLikeNet::BitStream out;
    WriteRpcPacket(id, &payload, out);

    SLikeNet::Packet packet = {};
    GetLastSender(packet.systemAddress, packet.guid);
    packet.length = out.GetNumberOfBytesUsed();
    packet.bitSize = out.GetNumberOfBitsUsed();
    packet.data = out.GetData();
    packet.deleteData = false;

    return EmulateRpcPacket(&packet);
}

bool HassleNet::EmulatePacket(SLikeNet::BitStream& payload) {
    SLikeNet::RakPeerInterface* peer = GetPeer();
    if (!peer)
        return false;

    const unsigned int bytes = payload.GetNumberOfBytesUsed();
    SLikeNet::Packet* packet = peer->AllocatePacket(bytes);
    if (!packet)
        return false;

    if (bytes > 0)
        memcpy(packet->data, payload.GetData(), bytes);
    packet->length = bytes;
    packet->bitSize = payload.GetNumberOfBitsUsed();
    GetLastSender(packet->systemAddress, packet->guid);

    peer->PushBackPacket(packet, false);
    return true;
}

int HassleNet::Detail::ParseRpcId(const char* name) {
    if (!name || !*name)
        return -1;

    char* end = nullptr;
    const long value = strtol(name, &end, 10);
    if (end == name || *end)
        return -1;
    return static_cast<int>(value);
}
