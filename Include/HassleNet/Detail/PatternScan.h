#pragma once

#include <cstddef>
#include <cstdint>

namespace HassleNet::Detail {
    struct ModuleInfo {
        std::uint8_t* base = nullptr;
        std::size_t size = 0;
        const std::uint8_t* text = nullptr;
        std::size_t textSize = 0;
    };

    const ModuleInfo& Module();
    void* FindPattern(const char* pattern, std::size_t* matchCount = nullptr);
}
