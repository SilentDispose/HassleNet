# HassleNet

Client-side **SLikeNet** hooking library for **Hassle Online** (Unreal Engine 5).
It intercepts the game's RakNet layer (RPC4 plugin + `RakPeer`) and exposes a small,
blocking, sending and emulating network traffic.

## Usage

```cpp
#include "HassleNet/HassleNet.h"

if (!HassleNet::Initialize())
    return;

HassleNet::OnReceiveRpc([](int& id, SLikeNet::BitStream* bs) {
    return true;
});

HassleNet::OnSendRpc([](int& id, SLikeNet::BitStream* bs,
                        SLikeNet::PacketPriority& priority,
                        SLikeNet::PacketReliability& reliability,
                        char& orderingChannel) {
    return true;
});

HassleNet::OnReceivePacket([](SLikeNet::Packet* packet) {
    return true;
});

HassleNet::OnSendPacket([](SLikeNet::BitStream* bs,
                           SLikeNet::PacketPriority& priority,
                           SLikeNet::PacketReliability& reliability,
                           char& orderingChannel) {
    return true;
});
```

Sending and emulating:

```cpp
SLikeNet::BitStream payload;
payload.Write((unsigned char)0x10);

HassleNet::SendRpc(42, &payload);       // to the server
HassleNet::SendPacket(&payload);        // raw BitStream send
HassleNet::SendPacketRaw(data, length); // raw buffer send

HassleNet::EmulateRpc(42, payload);     // run local RPC handling immediately
HassleNet::EmulatePacket(payload);      // enqueue as an incoming packet
```

Filtering without handlers:

```cpp
HassleNet::BlockRpc(42);
HassleNet::BlockPacketId(SLikeNet::ID_CONNECTION_LOST);
```

### Public API

| Function | Purpose |
|---|---|
| `Initialize()` / `Destroy()` / `IsInitialized()` | library lifetime |
| `IsReady()` | peer and RPC4 have been captured |
| `GetPeer()` / `SetPeer()` | `SLikeNet::RakPeerInterface*` |
| `GetRpc4()` / `SetRpc4()` | raw RPC4 plugin pointer |
| `OnReceiveRpc` / `OnSendRpc` | RPC handlers |
| `OnReceivePacket` / `OnSendPacket` | packet handlers |
| `BlockRpc(id, block)` / `IsRpcBlocked(id)` | RPC id filter |
| `BlockPacketId(id, block)` / `IsPacketIdBlocked(id)` | message id filter |
| `SendRpc` / `SendPacket` / `SendPacketRaw` | send to server |
| `EmulateRpc` / `EmulatePacket` | local injection |
| `ClearHandlers()` | drop all handlers |

## License

HassleNet is MIT-licensed, see [LICENSE](LICENSE).

Ported SLikeNet/`RakNet` code remains under its original licenses
(BSD-style, Copyright (c) 2014 Oculus VR; MIT-style modifications,
Copyright (c) 2017–2019 SLikeSoft UG). kthook and its dependencies retain their respective
licenses in the kthook repository.
