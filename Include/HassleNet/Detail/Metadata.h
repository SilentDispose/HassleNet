#pragma once

#include <cstddef>
#include <cstdint>

namespace HassleNet::Detail {
    struct ModuleInfo {
        std::uint8_t* base = nullptr;
        std::size_t size = 0;
        const std::uint8_t* textBegin = nullptr;
        const std::uint8_t* textEnd = nullptr;
        const std::uint8_t* rdataBegin = nullptr;
        const std::uint8_t* rdataEnd = nullptr;
    };

    struct MetadataState {
        ModuleInfo module;
        std::uint64_t classStorage = 0;
        bool located = false;
    };

    bool TryReadQword(const void* address, std::uint64_t& value);
    bool ValidateObjectVtable(const ModuleInfo& module, const void* object);
    bool LocateMetadata(ModuleInfo& module, std::uint64_t& classStorage);
    void* FindClassInstance(const ModuleInfo& module, void* uclass);
    void* ReadVirtualSlot(const ModuleInfo& module, void* object, std::size_t index);
}
