#include "HassleNet/HassleNet.h"

#include "HassleNet/Detail/Internal.h"
#include "HassleNet/SLikeNet/StringCompressor.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace HassleNet {
    namespace {
        struct Config {
            std::vector<ReceiveRpcHandler> receiveRpc;
            std::vector<SendRpcHandler> sendRpc;
            std::vector<ReceivePacketHandler> receivePacket;
            std::vector<SendPacketHandler> sendPacket;
            std::unordered_set<int> blockedRpc;
            std::unordered_set<unsigned char> blockedPacketIds;
        };

        std::mutex configMutex;
        std::mutex lifecycleMutex;
        std::shared_ptr<const Config> config = std::make_shared<Config>();
        std::atomic<bool> initialized = false;

        template <class Fn> void MutateConfig(Fn&& fn) {
            std::lock_guard lock(configMutex);
            auto next = std::make_shared<Config>(*config);
            fn(*next);
            config = std::move(next);
        }

        SLikeNet::AddressOrGUID ServerTarget() {
            return SLikeNet::AddressOrGUID(SLikeNet::UNASSIGNED_SYSTEM_ADDRESS());
        }

        std::shared_ptr<const Config> SnapshotConfig() {
            std::lock_guard lock(configMutex);
            return config;
        }

        void RestoreReadOffset(SLikeNet::BitStream* bs, SLikeNet::BitSize_t offset) {
            if (!bs)
                return;
            const SLikeNet::BitSize_t used = bs->GetNumberOfBitsUsed();
            bs->SetReadOffset(offset <= used ? offset : used);
        }

        void RewindForHandler(SLikeNet::BitStream* bs, SLikeNet::BitSize_t offset) {
            if (!bs)
                return;
            const SLikeNet::BitSize_t used = bs->GetNumberOfBitsUsed();
            bs->SetReadOffset(offset <= used ? offset : 0);
        }
    }

    namespace Detail {
        int ParseRpcId(const char* name) {
            if (!name || !*name)
                return -1;
            char* end = nullptr;
            const long value = strtol(name, &end, 10);
            if (end == name || *end != 0)
                return -1;
            return static_cast<int>(value);
        }

        bool DispatchReceivePacket(SLikeNet::Packet* packet) {
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

        bool DispatchSendPacket(SLikeNet::BitStream* bitStream,
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

        bool DispatchReceiveRpc(int& id, SLikeNet::BitStream* payload) {
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

        bool DispatchSendRpc(int& id,
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
    }

    bool Initialize() {
        if (initialized.load(std::memory_order_acquire))
            return true;

        std::lock_guard lock(lifecycleMutex);
        if (initialized.load(std::memory_order_relaxed))
            return true;

        if (!Detail::InstallHooks())
            return false;

        initialized.store(true, std::memory_order_release);
        return true;
    }

    void Destroy() {
        std::lock_guard lock(lifecycleMutex);
        if (!initialized.exchange(false, std::memory_order_acq_rel))
            return;

        Detail::RemoveHooks();
        ClearHandlers();
    }

    bool IsInitialized() {
        return initialized.load(std::memory_order_acquire);
    }

    void OnReceiveRpc(ReceiveRpcHandler handler) {
        if (!handler)
            return;
        MutateConfig([&](Config& cfg) { cfg.receiveRpc.push_back(std::move(handler)); });
    }

    void OnSendRpc(SendRpcHandler handler) {
        if (!handler)
            return;
        MutateConfig([&](Config& cfg) { cfg.sendRpc.push_back(std::move(handler)); });
    }

    void OnReceivePacket(ReceivePacketHandler handler) {
        if (!handler)
            return;
        MutateConfig([&](Config& cfg) { cfg.receivePacket.push_back(std::move(handler)); });
    }

    void OnSendPacket(SendPacketHandler handler) {
        if (!handler)
            return;
        MutateConfig([&](Config& cfg) { cfg.sendPacket.push_back(std::move(handler)); });
    }

    void ClearHandlers() {
        MutateConfig([](Config& cfg) {
            cfg.receiveRpc.clear();
            cfg.sendRpc.clear();
            cfg.receivePacket.clear();
            cfg.sendPacket.clear();
        });
    }

    void BlockRpc(int id, bool block) {
        MutateConfig([&](Config& cfg) {
            if (block)
                cfg.blockedRpc.insert(id);
            else
                cfg.blockedRpc.erase(id);
        });
    }

    void BlockPacketId(unsigned char id, bool block) {
        MutateConfig([&](Config& cfg) {
            if (block)
                cfg.blockedPacketIds.insert(id);
            else
                cfg.blockedPacketIds.erase(id);
        });
    }

    bool IsRpcBlocked(int id) {
        return SnapshotConfig()->blockedRpc.count(id) != 0;
    }

    bool IsPacketIdBlocked(unsigned char id) {
        return SnapshotConfig()->blockedPacketIds.count(id) != 0;
    }

    bool IsReady() {
        return Detail::PeerRaw() != nullptr;
    }

    SLikeNet::RakPeerInterface* GetPeer() {
        return reinterpret_cast<SLikeNet::RakPeerInterface*>(Detail::PeerRaw());
    }

    void* GetRpc4() {
        return Detail::Rpc4Raw();
    }

    void SetPeer(SLikeNet::RakPeerInterface* peer) {
        Detail::CapturePeer(peer);
    }

    void SetRpc4(void* rpc4) {
        Detail::CaptureRpc4(rpc4);
    }

    bool SendRpc(int id,
                 SLikeNet::BitStream* bs,
                 SLikeNet::PacketPriority priority,
                 SLikeNet::PacketReliability reliability,
                 char orderingChannel) {
        if (IsRpcBlocked(id))
            return false;
        void* rpc4 = Detail::Rpc4Raw();
        if (!rpc4 || !Detail::origRpcSignal)
            return false;
        char name[16];
        snprintf(name, sizeof(name), "%d", id);
        Detail::origRpcSignal(
            rpc4, name, bs, priority, reliability, orderingChannel, ServerTarget(), false, false);
        return true;
    }

    bool SendPacket(SLikeNet::BitStream* bs,
                    SLikeNet::PacketPriority priority,
                    SLikeNet::PacketReliability reliability,
                    char orderingChannel) {
        SLikeNet::RakPeerInterface* peer = GetPeer();
        if (!peer || !bs)
            return false;
        peer->Send(bs, priority, reliability, orderingChannel, ServerTarget(), false);
        return true;
    }

    bool SendPacketRaw(const void* data,
                       int length,
                       SLikeNet::PacketPriority priority,
                       SLikeNet::PacketReliability reliability,
                       char orderingChannel) {
        SLikeNet::RakPeerInterface* peer = GetPeer();
        if (!peer || !data || length <= 0)
            return false;
        peer->Send(reinterpret_cast<const char*>(data),
                   length,
                   priority,
                   reliability,
                   orderingChannel,
                   ServerTarget(),
                   false);
        return true;
    }

    bool EmulateRpc(int id, SLikeNet::BitStream& payload) {
        void* rpc4 = Detail::Rpc4Raw();
        if (!rpc4 || !Detail::origRpcOnReceive)
            return false;

        SLikeNet::BitStream out;
        out.Write(static_cast<unsigned char>(SLikeNet::ID_RPC_PLUGIN));
        out.Write(static_cast<unsigned char>(2));
        char name[16];
        snprintf(name, sizeof(name), "%d", id);
        SLikeNet::StringCompressor::Instance()->EncodeString(name, 0, &out, 0);
        out.AlignWriteToByteBoundary();
        if (payload.GetNumberOfBitsUsed() > 0)
            out.WriteBits(payload.GetData(), payload.GetNumberOfBitsUsed(), false);

        SLikeNet::Packet packet{};
        Detail::GetLastSender(packet.systemAddress, packet.guid);
        packet.length = out.GetNumberOfBytesUsed();
        packet.bitSize = out.GetNumberOfBitsUsed();
        packet.data = out.GetData();
        packet.deleteData = false;

        return Detail::EmulateRpcPacket(&packet);
    }

    bool EmulatePacket(SLikeNet::BitStream& payload) {
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
        Detail::GetLastSender(packet->systemAddress, packet->guid);

        peer->PushBackPacket(packet, false);
        return true;
    }
}
