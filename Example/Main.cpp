#include "HassleNet/HassleNet.h"

#include <cstdio>
#include <windows.h>

static void AttachConsole() {
    AllocConsole();
    FILE* file = nullptr;
    freopen_s(&file, "CONOUT$", "w", stdout);
}

static void Initialize() {
    AttachConsole();

    if (!HassleNet::Initialize()) {
        return;
    }

    HassleNet::OnReceiveRpc([](int& id, SLikeNet::BitStream* bs) {
        printf("[OnReceiveRpc] id: %d | bs: %p\n", id, bs);
        return true;
    });
    HassleNet::OnSendRpc([](int& id,
                            SLikeNet::BitStream* bs,
                            SLikeNet::PacketPriority& priority,
                            SLikeNet::PacketReliability& reliability,
                            char& orderingChannel) {
        printf(
            "[OnSendRpc] id: %d | bs: %p | priority: %d | reliability: %d | orderingChannel: %d\n",
            id,
            bs,
            priority,
            reliability,
            orderingChannel);
        return true;
    });
    HassleNet::OnReceivePacket([](SLikeNet::Packet* packet) {
        SLikeNet::BitStream bs(packet->data, packet->length);
        uint8_t id = 0;
        bs.Read(id);
        printf("[OnReceivePacket] id: %d | packet: %p\n", id, packet);
        return true;
    });
    HassleNet::OnSendPacket([](SLikeNet::BitStream* bs,
                               SLikeNet::PacketPriority& priority,
                               SLikeNet::PacketReliability& reliability,
                               char& orderingChannel) {
        printf("[OnSendPacket] id: %d | bs: %p | priority: %d | reliability: %d | "
               "orderingChannel: %d\n",
               bs->GetData()[0],
               bs,
               priority,
               reliability,
               orderingChannel);
        return true;
    });
}

extern "C" __declspec(dllexport) void InitializeASI() {
    Initialize();
}

extern "C" __declspec(dllexport) void ShutdownASI() {
    HassleNet::Destroy();
}

BOOL APIENTRY DllMain(HMODULE handle, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(handle);
    return TRUE;
}
