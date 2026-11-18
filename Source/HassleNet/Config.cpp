#include "HassleNet/Detail/Internal.h"

#include <cstring>
#include <memory>
#include <mutex>

static std::mutex configMutex;
static HassleNet::Detail::ConfigPtr config = std::make_shared<HassleNet::Detail::Config>();

static void RestoreReadOffset(SLikeNet::BitStream* bs, SLikeNet::BitSize_t offset) {
    if (!bs)
        return;
    const SLikeNet::BitSize_t used = bs->GetNumberOfBitsUsed();
    bs->SetReadOffset(offset <= used ? offset : used);
}

static void RewindForHandler(SLikeNet::BitStream* bs, SLikeNet::BitSize_t offset) {
    if (!bs)
        return;
    const SLikeNet::BitSize_t used = bs->GetNumberOfBitsUsed();
    bs->SetReadOffset(offset <= used ? offset : 0);
}

HassleNet::Detail::ConfigPtr HassleNet::Detail::SnapshotConfig() {
    std::lock_guard lock(configMutex);
    return config;
}

void HassleNet::Detail::MutateConfig(const std::function<void(Config&)>& mutator) {
    std::lock_guard lock(configMutex);
    auto next = std::make_shared<Config>(*config);
    mutator(*next);
    config = std::move(next);
}

bool HassleNet::Detail::DispatchReceivePacket(SLikeNet::Packet* packet) {
    if (!packet || !packet->data || packet->length == 0)
        return true;

    const auto cfg = SnapshotConfig();

    if (cfg->blockedPacketIds.count(packet->data[0]))
        return false;

    if (cfg->receivePacket.empty())
        return true;

    for (const ReceivePacketHandler& handler : cfg->receivePacket) {
        if (!handler(packet))
            return false;
    }
    return true;
}

bool HassleNet::Detail::DispatchSendPacket(SLikeNet::BitStream* bitStream,
                                           SLikeNet::PacketPriority& priority,
                                           SLikeNet::PacketReliability& reliability,
                                           char& orderingChannel) {
    const auto cfg = SnapshotConfig();

    if (bitStream && bitStream->GetNumberOfBytesUsed() > 0 &&
        cfg->blockedPacketIds.count(bitStream->GetData()[0]))
        return false;

    if (cfg->sendPacket.empty())
        return true;

    const SLikeNet::BitSize_t readOffset = bitStream ? bitStream->GetReadOffset() : 0;
    for (const SendPacketHandler& handler : cfg->sendPacket) {
        RewindForHandler(bitStream, readOffset);
        if (!handler(bitStream, priority, reliability, orderingChannel)) {
            RestoreReadOffset(bitStream, readOffset);
            return false;
        }
    }
    RestoreReadOffset(bitStream, readOffset);
    return true;
}

bool HassleNet::Detail::DispatchReceiveRpc(int& id, SLikeNet::BitStream* payload) {
    const auto cfg = SnapshotConfig();

    if (cfg->blockedRpc.count(id))
        return false;

    if (cfg->receiveRpc.empty())
        return true;

    const SLikeNet::BitSize_t readOffset = payload ? payload->GetReadOffset() : 0;
    for (const ReceiveRpcHandler& handler : cfg->receiveRpc) {
        RewindForHandler(payload, readOffset);
        if (!handler(id, payload)) {
            RestoreReadOffset(payload, readOffset);
            return false;
        }
        if (cfg->blockedRpc.count(id)) {
            RestoreReadOffset(payload, readOffset);
            return false;
        }
    }
    RestoreReadOffset(payload, readOffset);
    return true;
}

bool HassleNet::Detail::DispatchSendRpc(int& id,
                                        SLikeNet::BitStream* payload,
                                        SLikeNet::PacketPriority& priority,
                                        SLikeNet::PacketReliability& reliability,
                                        char& orderingChannel) {
    const auto cfg = SnapshotConfig();

    if (cfg->blockedRpc.count(id))
        return false;

    if (cfg->sendRpc.empty())
        return true;

    const SLikeNet::BitSize_t readOffset = payload ? payload->GetReadOffset() : 0;
    for (const SendRpcHandler& handler : cfg->sendRpc) {
        RewindForHandler(payload, readOffset);
        if (!handler(id, payload, priority, reliability, orderingChannel)) {
            RestoreReadOffset(payload, readOffset);
            return false;
        }
        if (cfg->blockedRpc.count(id)) {
            RestoreReadOffset(payload, readOffset);
            return false;
        }
    }
    RestoreReadOffset(payload, readOffset);
    return true;
}
