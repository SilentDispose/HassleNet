#pragma once

#include "SLikeNet/BitStream.h"
#include "SLikeNet/MessageIdentifiers.h"
#include "SLikeNet/PeerInterface.h"
#include "SLikeNet/Types.h"

#include <functional>

namespace HassleNet {
    using SendPacketHandler = std::function<bool(SLikeNet::BitStream* bs,
                                                 SLikeNet::PacketPriority& priority,
                                                 SLikeNet::PacketReliability& reliability,
                                                 char& orderingChannel)>;
    using ReceivePacketHandler = std::function<bool(SLikeNet::Packet* packet)>;
    using SendRpcHandler = std::function<bool(int& id,
                                              SLikeNet::BitStream* bs,
                                              SLikeNet::PacketPriority& priority,
                                              SLikeNet::PacketReliability& reliability,
                                              char& orderingChannel)>;
    using ReceiveRpcHandler = std::function<bool(int& id, SLikeNet::BitStream* bs)>;

    bool Initialize();
    void Destroy();
    bool IsInitialized();

    void OnReceiveRpc(ReceiveRpcHandler handler);
    void OnSendRpc(SendRpcHandler handler);
    void OnReceivePacket(ReceivePacketHandler handler);
    void OnSendPacket(SendPacketHandler handler);
    void ClearHandlers();

    void BlockRpc(int id, bool block = true);
    void BlockPacketId(unsigned char id, bool block = true);
    bool IsRpcBlocked(int id);
    bool IsPacketIdBlocked(unsigned char id);

    bool IsReady();
    SLikeNet::RakPeerInterface* GetPeer();
    void* GetRpc4();
    void SetPeer(SLikeNet::RakPeerInterface* peer);
    void SetRpc4(void* rpc4);

    bool SendRpc(int id,
                 SLikeNet::BitStream* bs = nullptr,
                 SLikeNet::PacketPriority priority = SLikeNet::HIGH_PRIORITY,
                 SLikeNet::PacketReliability reliability = SLikeNet::RELIABLE_ORDERED,
                 char orderingChannel = 0);
    bool SendPacket(SLikeNet::BitStream* bs,
                    SLikeNet::PacketPriority priority = SLikeNet::HIGH_PRIORITY,
                    SLikeNet::PacketReliability reliability = SLikeNet::RELIABLE_ORDERED,
                    char orderingChannel = 0);
    bool SendPacketRaw(const void* data,
                       int length,
                       SLikeNet::PacketPriority priority = SLikeNet::HIGH_PRIORITY,
                       SLikeNet::PacketReliability reliability = SLikeNet::RELIABLE_ORDERED,
                       char orderingChannel = 0);

    bool EmulateRpc(int id, SLikeNet::BitStream& payload);
    bool EmulatePacket(SLikeNet::BitStream& payload);
}
