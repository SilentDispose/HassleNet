#pragma once

#include "HassleNet/HassleNet.h"

#include <functional>
#include <memory>
#include <unordered_set>
#include <vector>

namespace HassleNet::Detail {
    struct Config {
        std::vector<ReceiveRpcHandler> receiveRpc;
        std::vector<SendRpcHandler> sendRpc;
        std::vector<ReceivePacketHandler> receivePacket;
        std::vector<SendPacketHandler> sendPacket;
        std::unordered_set<int> blockedRpc;
        std::unordered_set<unsigned char> blockedPacketIds;
    };

    using ConfigPtr = std::shared_ptr<const Config>;

    ConfigPtr SnapshotConfig();
    void MutateConfig(const std::function<void(Config&)>& mutator);
}
