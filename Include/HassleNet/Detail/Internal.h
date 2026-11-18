#pragma once

#include "HassleNet/Detail/Config.h"
#include "HassleNet/Detail/Hooks.h"
#include "HassleNet/Detail/Metadata.h"

namespace HassleNet::Detail {
    bool DispatchReceivePacket(SLikeNet::Packet* packet);
    bool DispatchSendPacket(SLikeNet::BitStream* bitStream,
                            SLikeNet::PacketPriority& priority,
                            SLikeNet::PacketReliability& reliability,
                            char& orderingChannel);
    int ParseRpcId(const char* name);
    void WriteRpcPacket(int id, const SLikeNet::BitStream* payload, SLikeNet::BitStream& out);
    bool DispatchReceiveRpc(int& id, SLikeNet::BitStream* payload);
    bool DispatchSendRpc(int& id,
                         SLikeNet::BitStream* payload,
                         SLikeNet::PacketPriority& priority,
                         SLikeNet::PacketReliability& reliability,
                         char& orderingChannel);
}
