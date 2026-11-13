#pragma once

#include "BitStream.h"

#include <cstddef>

namespace SLikeNet {
    struct HuffmanEncodingTreeNode {
        HuffmanEncodingTreeNode* left;
        HuffmanEncodingTreeNode* right;
        HuffmanEncodingTreeNode* parent;
        unsigned char value;
        unsigned int weight;
    };

    class HuffmanEncodingTree {
    public:
        HuffmanEncodingTree();
        ~HuffmanEncodingTree();

        HuffmanEncodingTree(const HuffmanEncodingTree&) = delete;
        HuffmanEncodingTree& operator=(const HuffmanEncodingTree&) = delete;

        void GenerateFromFrequencyTable(unsigned int frequencyTable[256]);
        void EncodeArray(unsigned char* input, size_t sizeInBytes, BitStream* output);
        unsigned DecodeArray(BitStream* input,
                             BitSize_t sizeInBits,
                             size_t maxCharsToWrite,
                             unsigned char* output);
        void DecodeArray(unsigned char* input, BitSize_t sizeInBits, BitStream* output);

    private:
        void FreeMemory();

        struct EncodingTableEntry {
            unsigned char bitLength;
            unsigned char* encoding;
        };

        EncodingTableEntry encodingTable[256];
        HuffmanEncodingTreeNode* root;
    };
}
