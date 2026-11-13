# HassleNet

Client-side **SLikeNet** hooking library for **Hassle Online** (Unreal Engine 5.8).
It intercepts the game's RakNet layer (RPC4 plugin + `RakPeer`) and exposes a small,
rakhook-style API for reading, blocking, sending and emulating network traffic.

Everything is resolved at startup from byte patterns — no hardcoded absolute addresses.

## Supported game build

| | |
|---|---|
| Game | Hassle Online, build **2.1.0.8** |
| Executable | `HassleOnline-Win64-Shipping.exe`, 182 231 472 bytes, MD5 `b81ade40650bbf68dac148670810bfbd` |
| Networking | SLikeNet **0.1.3** (RakNet 4.082-compatible, protocol version 6), statically linked into the exe |
| Original | `Plugins/RakNet/Source/RakNet/Private/SLikeNet/...` |

`HassleNet::Initialize()` returns `true` only when all hook signatures were found
uniquely, so it doubles as a build compatibility check.

## Features

- RPC4: `OnReceiveRpc` / `OnSendRpc` with a mutable `int& id` and payload `BitStream*`.
- Packets: `OnReceivePacket` (`SLikeNet::Packet*`) and `OnSendPacket` (mutable priority/reliability/channel).
- Returning `false` from a handler drops the RPC/packet.
- RPC id and payload can be modified on the fly; the packet is re-serialized before the game sees it.
- `SendRpc` / `SendPacket` / `SendPacketRaw` to the connected server.
- `EmulateRpc` (synchronous local dispatch) and `EmulatePacket` (queued receive).
- ABI-exact `SLikeNet::BitStream`, protocol types and `StringCompressor`
  (byte-identical wire format, verified against upstream SLikeNet).
- Full `RakPeerInterface` access through `GetPeer()`.
- Static library: MinHook is linked in, the consumer only links `HassleNet.lib`.

## Build

Requirements: Windows x64, MSVC 2022 (or newer), CMake 3.20+.
MinHook is fetched automatically by CMake.

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Outputs:

- `build/Release/HassleNet.lib` — static library;
- `build/Example/<config>/HassleNetExample.asi` — example plugin (ASI, inject into the game process).

## Layout

```
Include/HassleNet/
    HassleNet.h              public API
    Detail/Internal.h        hook/dispatch plumbing (library use)
    Detail/PatternScan.h     byte signature scanner
    SLikeNet/BitStream.h
    SLikeNet/DS_HuffmanEncodingTree.h
    SLikeNet/MessageIdentifiers.h
    SLikeNet/PeerInterface.h
    SLikeNet/StringCompressor.h
    SLikeNet/Types.h
Source/                      implementation
Example/                     example ASI plugin
```

## Usage

```cpp
#include "HassleNet/HassleNet.h"

// Must be called after the game process is up (not from inside DllMain's loader lock).
if (!HassleNet::Initialize())
    return;

HassleNet::OnReceiveRpc([](int& id, SLikeNet::BitStream* bs) {
    // returning false drops the RPC before the game handler runs
    return true;
});

HassleNet::OnSendRpc([](int& id, SLikeNet::BitStream* bs,
                        SLikeNet::PacketPriority& priority,
                        SLikeNet::PacketReliability& reliability,
                        char& orderingChannel) {
    return true;
});

HassleNet::OnReceivePacket([](SLikeNet::Packet* packet) {
    // packet->data[0] = message id, packet->systemAddress = sender
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

## Hooked functions (build 2.1.0.8)

RVAs are relative to the image base `0x140000000`; at runtime each address is found by a
unique 32-byte signature, not by these constants.

| Function | RVA | Hooked for |
|---|---|---|
| `RakPeer::Receive` | `0x57244C0` | `OnReceivePacket`, peer capture, blocking (deallocate + next) |
| `RakPeer::Send(BitStream*)` | `0x5727800` | `OnSendPacket` |
| `RakPeer::Send(char*, int)` | `0x5727620` | `OnSendPacket` (raw) |
| `RakPeer::DeallocatePacket` | `0x571C8A0` | dropping blocked packets |
| `RPC4::Signal` | `0x5733340` | `OnSendRpc` |
| `RPC4::OnReceive` | `0x5730A50` | `OnReceiveRpc` (+ id/payload rewrite) |

## Behaviour notes

- **RPC ids are numeric.** The game's wrapper converts a `uint8` id to a decimal string
  (`FString::FromInt`) and uses it as the RPC4 shared identifier. HassleNet parses it back
  to `int`; a non-numeric identifier yields `id == -1`.
- Only `ID_RPC4_SIGNAL` is processed; `ID_RPC4_CALL` / `ID_RPC4_RETURN` pass through untouched.
- `OnReceiveRpc` runs inside `RPC4::OnReceive`, before the game handler. Changing `id` or
  writing to the payload rebuilds the packet; returning `false` returns
  `RR_STOP_PROCESSING_AND_DEALLOCATE`.
- `OnReceivePacket` is hooked at `RakPeer::Receive`, i.e. it only sees packets that were
  not consumed by a plugin. RPCs and game packets are consumed inside `Receive` by the
  game's own plugin, so use `OnReceiveRpc` for them.
- `EmulateRpc` bypasses the network and runs the local receive path synchronously, using
  the last known sender (server) address. `EmulatePacket` allocates a `Packet` via
  `RakPeer::AllocatePacket` and pushes it into the receive queue.
- The library is client-side: there are no player-list / kick / ban APIs. Everything else
  is available through `GetPeer()` (`SLikeNet::RakPeerInterface*`).

## SLikeNet compatibility

The vendored headers replicate the exact ABI of the shipping build:

- `BitStream` — 288 bytes, `readOffset@8`, `data@16`, `copyData@24`, `stackData@25`;
- `SystemAddress` 20, `RakNetGUID` 16, `AddressOrGUID` 40, `Packet` 64 bytes;
- `RakPeerInterface` — all 87 virtual slots in the binary's order
  (`Send(BitStream*)` precedes `Send(char*, int)`);
- compressed numbers are written big-endian, matching SLikeNet builds without
  `__BITSTREAM_NATIVE_END`;
- `StringCompressor` output was diffed byte-for-byte against upstream SLikeNet.

## Determining the game version

The executable carries no useful version resource (`UE5-CL-0`). Use the launcher manifest:

```
<GameRoot>/manifest.json  ->  "build": "2.1.0.8"
```

The manifest also lists every shipped file with its MD5, including the executable.
Alternatively compare the executable's own MD5/SHA-256, or rely on
`HassleNet::Initialize()` returning `true` as the "supported build" check.

## License

HassleNet is MIT-licensed, see [LICENSE](LICENSE).

Ported SLikeNet/`RakNet` code remains under its original licenses
(BSD-style, Copyright (c) 2014 Oculus VR; MIT-style modifications,
Copyright (c) 2017–2019 SLikeSoft UG). MinHook is BSD 2-Clause.
