#include "HassleNet/SLikeNet/StringCompressor.h"

#include <cstdlib>
#include <cstring>

namespace SLikeNet {
    namespace {
        unsigned int englishCharacterFrequencies[256] = {
            0,   0,    0,    0,    0,     0,    0,    0,    0,   0,    722,  0,    0,    2,
            0,   0,    0,    0,    0,     0,    0,    0,    0,   0,    0,    0,    0,    0,
            0,   0,    0,    0,    11084, 58,   63,   1,    0,   31,   0,    317,  64,   64,
            44,  0,    695,  62,   980,   266,  69,   67,   56,  7,    73,   3,    14,   2,
            69,  1,    167,  9,    1,     2,    25,   94,   0,   195,  139,  34,   96,   48,
            103, 56,   125,  653,  21,    5,    23,   64,   85,  44,   34,   7,    92,   76,
            147, 12,   14,   57,   15,    39,   15,   1,    1,   1,    2,    3,    0,    3611,
            845, 1077, 1884, 5870, 841,   1057, 2501, 3212, 164, 531,  2019, 1330, 3056, 4037,
            848, 47,   2586, 2919, 4771,  1707, 535,  1106, 152, 1243, 100,  0,    2,    0,
            10,  0,    0,    0,    0,     0,    0,    0,    0,   0,    0,    0,    0,    0,
            0,   0,    0,    0,    0,     0,    0,    0,    0,   0,    0,    0,    0,    0,
            0,   0,    0,    0,    0,     0,    0,    0,    0,   0,    0,    0,    0,    0,
            0,   0,    0,    0,    0,     0,    0,    0,    0,   0,    0,    0,    0,    0,
            0,   0,    0,    0,    0,     0,    0,    0,    0,   0,    0,    0,    0,    0,
            0,   0,    0,    0,    0,     0,    0,    0,    0,   0,    0,    0,    0,    0,
            0,   0,    0,    0,    0,     0,    0,    0,    0,   0,    0,    0,    0,    0,
            0,   0,    0,    0,    0,     0,    0,    0,    0,   0,    0,    0,    0,    0,
            0,   0,    0,    0,    0,     0,    0,    0,    0,   0,    0,    0,    0,    0,
            0,   0,    0,    0,
        };
    }

    StringCompressor::StringCompressor() {
        auto tree = std::make_unique<HuffmanEncodingTree>();
        tree->GenerateFromFrequencyTable(englishCharacterFrequencies);
        huffmanEncodingTrees.emplace(0, std::move(tree));
    }

    StringCompressor* StringCompressor::Instance() {
        static StringCompressor instance;
        return &instance;
    }

    void StringCompressor::AddReference() {}

    void StringCompressor::RemoveReference() {}

    void StringCompressor::GenerateTreeFromStrings(unsigned char* input,
                                                   unsigned inputLength,
                                                   uint8_t languageId) {
        if (inputLength == 0)
            return;

        unsigned int frequencyTable[256];
        memset(frequencyTable, 0, sizeof(frequencyTable));
        for (unsigned index = 0; index < inputLength; ++index)
            frequencyTable[input[index]]++;

        auto tree = std::make_unique<HuffmanEncodingTree>();
        tree->GenerateFromFrequencyTable(frequencyTable);
        huffmanEncodingTrees[languageId] = std::move(tree);
    }

    void StringCompressor::EncodeString(const char* input,
                                        int maxCharsToWrite,
                                        BitStream* output,
                                        uint8_t languageId) {
        auto it = huffmanEncodingTrees.find(languageId);
        if (it == huffmanEncodingTrees.end())
            return;

        if (input == nullptr) {
            output->WriteCompressed((uint32_t)0);
            return;
        }

        int charsToWrite;
        if (maxCharsToWrite <= 0 || (int)strlen(input) < maxCharsToWrite)
            charsToWrite = (int)strlen(input);
        else
            charsToWrite = maxCharsToWrite - 1;

        BitStream encodedBitStream;
        it->second->EncodeArray((unsigned char*)input, (size_t)charsToWrite, &encodedBitStream);

        const uint32_t stringBitLength = (uint32_t)encodedBitStream.GetNumberOfBitsUsed();
        output->WriteCompressed(stringBitLength);
        output->WriteBits(encodedBitStream.GetData(), stringBitLength);
    }

    bool StringCompressor::DecodeString(char* output,
                                        int maxCharsToWrite,
                                        BitStream* input,
                                        uint8_t languageId) {
        auto it = huffmanEncodingTrees.find(languageId);
        if (it == huffmanEncodingTrees.end())
            return false;
        if (maxCharsToWrite <= 0)
            return false;

        output[0] = 0;

        uint32_t stringBitLength = 0;
        if (!input->ReadCompressed(stringBitLength))
            return false;

        if ((unsigned)input->GetNumberOfUnreadBits() < stringBitLength)
            return false;

        const int bytesInStream = (int)it->second->DecodeArray(
            input, stringBitLength, (size_t)maxCharsToWrite, (unsigned char*)output);

        if (bytesInStream < maxCharsToWrite)
            output[bytesInStream] = 0;
        else
            output[maxCharsToWrite - 1] = 0;

        return true;
    }
}
