#pragma once

#include "BitStream.h"
#include "DS_HuffmanEncodingTree.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace SLikeNet {
    class StringCompressor {
    public:
        StringCompressor();
        ~StringCompressor() = default;

        StringCompressor(const StringCompressor&) = delete;
        StringCompressor& operator=(const StringCompressor&) = delete;

        static StringCompressor* Instance();

        static void AddReference();
        static void RemoveReference();

        void
        GenerateTreeFromStrings(unsigned char* input, unsigned inputLength, uint8_t languageId);

        void EncodeString(const char* input,
                          int maxCharsToWrite,
                          BitStream* output,
                          uint8_t languageId = 0);
        bool
        DecodeString(char* output, int maxCharsToWrite, BitStream* input, uint8_t languageId = 0);

    private:
        std::unordered_map<int, std::unique_ptr<HuffmanEncodingTree>> huffmanEncodingTrees;
    };
}
