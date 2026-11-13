#include "HassleNet/HassleNet.h"

#include <string>
#include <thread>
#include <windows.h>

static void AttachConsole() {
    if (!AllocConsole()) {
        return;
    }
    FILE* file;
    freopen_s(&file, "CONOUT$", "w", stdout);
}

static void MainThread() {
    AttachConsole();

    while (!HassleNet::Initialize())
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

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

BOOL APIENTRY DllMain(HMODULE handle, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(handle);
            std::thread(MainThread).detach();
            break;
        }
        case DLL_PROCESS_DETACH: {
            HassleNet::Destroy();
            break;
        }
    }
    return TRUE;
}
