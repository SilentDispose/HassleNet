#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "HassleNet/Detail/Metadata.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <windows.h>

using namespace HassleNet::Detail;

static constexpr size_t kObjectClassOffset = 0x10;
static constexpr size_t kClassScanSize = 0x400;

static const wchar_t kClientClassName[] = L"URakNetClient";

static std::mutex metadataMutex;
static MetadataState metadataState;

static bool GetModuleInfo(ModuleInfo& info) {
    info = {};
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!base)
        return false;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    info.base = base;
    info.size = nt->OptionalHeader.SizeOfImage;

    auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        auto* begin = base + section->VirtualAddress;
        size_t size = section->Misc.VirtualSize > section->SizeOfRawData ? section->Misc.VirtualSize
                                                                         : section->SizeOfRawData;

        if (!memcmp(section->Name, ".text", 5)) {
            info.textBegin = begin;
            info.textEnd = begin + size;
        } else if (!memcmp(section->Name, ".rdata", 6)) {
            info.rdataBegin = begin;
            info.rdataEnd = begin + size;
        }
    }
    return info.textBegin && info.rdataBegin;
}

static bool InRange(const void* value, const unsigned char* begin, const unsigned char* end) {
    const auto* address = static_cast<const unsigned char*>(value);
    return address >= begin && address < end;
}

static const unsigned char* FindClientClassName(const ModuleInfo& module) {
    const auto* needle = reinterpret_cast<const unsigned char*>(kClientClassName);
    constexpr size_t needleSize = sizeof(kClientClassName);

    for (auto* cursor = module.rdataBegin; cursor + needleSize <= module.rdataEnd; ++cursor) {
        if (cursor[0] == 'U' && !cursor[1] && !memcmp(cursor, needle, needleSize))
            return cursor;
    }
    return nullptr;
}

static uint64_t FindClientClassStorage(const ModuleInfo& module) {
    const auto* name = FindClientClassName(module);
    if (!name)
        return 0;

    const auto nameValue = reinterpret_cast<uint64_t>(name);
    for (size_t offset = sizeof(void*); offset <= 0x80; offset += sizeof(void*)) {
        const auto* slot = reinterpret_cast<const uint64_t*>(name - offset);
        uint64_t value = 0;
        uint64_t storage = 0;

        if (TryReadQword(slot, value) && value == nameValue && TryReadQword(slot + 1, storage) &&
            storage)
            return storage;
    }
    return 0;
}

bool HassleNet::Detail::TryReadQword(const void* address, uint64_t& value) {
    __try {
        value = *static_cast<const uint64_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool HassleNet::Detail::ValidateObjectVtable(const ModuleInfo& module, const void* object) {
    uint64_t vtable = 0;
    return TryReadQword(object, vtable) &&
           InRange(reinterpret_cast<const void*>(vtable), module.rdataBegin, module.rdataEnd);
}

bool HassleNet::Detail::LocateMetadata(ModuleInfo& module, uint64_t& classStorage) {
    std::lock_guard lock(metadataMutex);
    if (!metadataState.located) {
        if (!GetModuleInfo(metadataState.module))
            return false;
        metadataState.classStorage = FindClientClassStorage(metadataState.module);
        metadataState.located = true;
    }

    module = metadataState.module;
    classStorage = metadataState.classStorage;
    return classStorage != 0;
}

void* HassleNet::Detail::FindClassInstance(const ModuleInfo& module, void* uclass) {
    const auto uclassValue = reinterpret_cast<uint64_t>(uclass);
    const auto* bytes = static_cast<const unsigned char*>(uclass);

    for (size_t offset = 0; offset < kClassScanSize; offset += sizeof(void*)) {
        uint64_t candidate = 0;
        uint64_t candidateClass = 0;
        uint64_t vtable = 0;

        if (!TryReadQword(bytes + offset, candidate) || candidate < 0x10000)
            continue;
        if (!TryReadQword(reinterpret_cast<const void*>(candidate + kObjectClassOffset),
                          candidateClass) ||
            candidateClass != uclassValue)
            continue;
        if (!TryReadQword(reinterpret_cast<const void*>(candidate), vtable) ||
            !InRange(reinterpret_cast<const void*>(vtable), module.rdataBegin, module.rdataEnd))
            continue;

        return reinterpret_cast<void*>(candidate);
    }
    return nullptr;
}

void* HassleNet::Detail::ReadVirtualSlot(const ModuleInfo& module, void* object, size_t index) {
    uint64_t vtable = 0;
    uint64_t target = 0;

    if (!TryReadQword(object, vtable) || !vtable)
        return nullptr;
    if (!InRange(reinterpret_cast<const void*>(vtable), module.rdataBegin, module.rdataEnd))
        return nullptr;
    if (!TryReadQword(reinterpret_cast<const void*>(vtable + index * sizeof(void*)), target))
        return nullptr;
    if (!InRange(reinterpret_cast<const void*>(target), module.textBegin, module.textEnd))
        return nullptr;

    return reinterpret_cast<void*>(target);
}
