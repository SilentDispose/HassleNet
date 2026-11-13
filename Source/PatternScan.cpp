#include "HassleNet/Detail/PatternScan.h"

#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace HassleNet::Detail {
    namespace {
        bool ParseNibble(char c, int& value) {
            if (c >= '0' && c <= '9') {
                value = c - '0';
                return true;
            }
            if (c >= 'a' && c <= 'f') {
                value = c - 'a' + 10;
                return true;
            }
            if (c >= 'A' && c <= 'F') {
                value = c - 'A' + 10;
                return true;
            }
            return false;
        }

        ModuleInfo BuildModuleInfo() {
            ModuleInfo info;
#if defined(_WIN32)
            auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
            if (!base)
                return info;

            auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            info.base = base;
            info.size = nt->OptionalHeader.SizeOfImage;

            auto* section = IMAGE_FIRST_SECTION(nt);
            for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
                if (memcmp(section->Name, ".text", 5) == 0) {
                    info.text = base + section->VirtualAddress;
                    info.textSize = section->Misc.VirtualSize;
                    break;
                }
            }
#endif
            return info;
        }

        struct PatternByte {
            std::uint8_t value;
            std::uint8_t mask;
        };
    }

    const ModuleInfo& Module() {
        static const ModuleInfo info = BuildModuleInfo();
        return info;
    }

    void* FindPattern(const char* pattern, std::size_t* matchCount) {
        if (matchCount)
            *matchCount = 0;

        const ModuleInfo& module = Module();
        if (!module.text || !pattern)
            return nullptr;

        PatternByte bytes[256];
        std::size_t count = 0;

        for (const char* p = pattern; *p && count < 256;) {
            while (*p == ' ')
                ++p;
            if (!*p)
                break;
            if (p[0] == '?' && p[1] == '?') {
                bytes[count++] = {0, 0};
                p += 2;
                continue;
            }
            int hi = 0, lo = 0;
            if (!ParseNibble(p[0], hi) || !ParseNibble(p[1], lo))
                return nullptr;
            bytes[count++] = {(std::uint8_t)((hi << 4) | lo), 0xFF};
            p += 2;
        }

        if (count == 0 || count > module.textSize)
            return nullptr;

        const std::uint8_t* text = module.text;
        const std::size_t limit = module.textSize - count;
        void* first = nullptr;

        for (std::size_t i = 0; i <= limit; ++i) {
            std::size_t j = 0;
            for (; j < count; ++j) {
                if (bytes[j].mask && text[i + j] != bytes[j].value)
                    break;
            }
            if (j == count) {
                if (!first)
                    first = const_cast<std::uint8_t*>(text + i);
                if (matchCount)
                    ++*matchCount;
                if (!matchCount)
                    return first;
                if (*matchCount > 1)
                    break;
            }
        }
        return first;
    }
}
