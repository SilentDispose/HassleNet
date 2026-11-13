#include "HassleNet/SLikeNet/DS_HuffmanEncodingTree.h"

#include <cstdlib>
#include <cstring>
#include <list>

namespace SLikeNet {
    namespace {
        void InsertNodeIntoSortedList(HuffmanEncodingTreeNode* node,
                                      std::list<HuffmanEncodingTreeNode*>& list) {
            for (auto it = list.begin(); it != list.end(); ++it) {
                if ((*it)->weight >= node->weight) {
                    list.insert(it, node);
                    return;
                }
            }
            list.push_back(node);
        }
    }

    HuffmanEncodingTree::HuffmanEncodingTree()
        : root(nullptr) {
        memset(encodingTable, 0, sizeof(encodingTable));
    }

    HuffmanEncodingTree::~HuffmanEncodingTree() {
        FreeMemory();
    }

    void HuffmanEncodingTree::FreeMemory() {
        if (!root)
            return;

        std::list<HuffmanEncodingTreeNode*> queue;
        queue.push_back(root);
        while (!queue.empty()) {
            HuffmanEncodingTreeNode* node = queue.front();
            queue.pop_front();
            if (node->left)
                queue.push_back(node->left);
            if (node->right)
                queue.push_back(node->right);
            delete node;
        }

        for (int i = 0; i < 256; ++i) {
            free(encodingTable[i].encoding);
            encodingTable[i].encoding = nullptr;
        }

        root = nullptr;
    }

    void HuffmanEncodingTree::GenerateFromFrequencyTable(unsigned int frequencyTable[256]) {
        HuffmanEncodingTreeNode* leafList[256];
        std::list<HuffmanEncodingTreeNode*> nodes;

        FreeMemory();

        for (int counter = 0; counter < 256; ++counter) {
            auto* node = new HuffmanEncodingTreeNode;
            node->left = nullptr;
            node->right = nullptr;
            node->parent = nullptr;
            node->value = (unsigned char)counter;
            node->weight = frequencyTable[counter];
            if (node->weight == 0)
                node->weight = 1;

            leafList[counter] = node;
            InsertNodeIntoSortedList(node, nodes);
        }

        for (;;) {
            HuffmanEncodingTreeNode* lesser = nodes.front();
            nodes.pop_front();
            HuffmanEncodingTreeNode* greater = nodes.front();
            nodes.pop_front();

            auto* node = new HuffmanEncodingTreeNode;
            node->left = lesser;
            node->right = greater;
            node->parent = nullptr;
            node->value = 0;
            node->weight = lesser->weight + greater->weight;
            lesser->parent = node;
            greater->parent = node;

            if (nodes.empty()) {
                root = node;
                break;
            }

            InsertNodeIntoSortedList(node, nodes);
        }

        bool tempPath[256];
        unsigned short tempPathLength;
        BitStream bitStream;

        for (int counter = 0; counter < 256; ++counter) {
            tempPathLength = 0;
            HuffmanEncodingTreeNode* currentNode = leafList[counter];

            do {
                if (currentNode->parent->left == currentNode)
                    tempPath[tempPathLength++] = false;
                else
                    tempPath[tempPathLength++] = true;
                currentNode = currentNode->parent;
            } while (currentNode != root);

            while (tempPathLength-- > 0) {
                if (tempPath[tempPathLength])
                    bitStream.Write1();
                else
                    bitStream.Write0();
            }

            encodingTable[counter].bitLength =
                (unsigned char)bitStream.CopyData(&encodingTable[counter].encoding);
            bitStream.Reset();
        }
    }

    void
    HuffmanEncodingTree::EncodeArray(unsigned char* input, size_t sizeInBytes, BitStream* output) {
        for (size_t counter = 0; counter < sizeInBytes; ++counter) {
            output->WriteBits(encodingTable[input[counter]].encoding,
                              encodingTable[input[counter]].bitLength,
                              false);
        }

        if (output->GetNumberOfBitsUsed() % 8 != 0) {
            const unsigned char remainingBits =
                (unsigned char)(8 - (output->GetNumberOfBitsUsed() % 8));
            for (int counter = 0; counter < 256; ++counter) {
                if (encodingTable[counter].bitLength > remainingBits) {
                    output->WriteBits(encodingTable[counter].encoding, remainingBits, false);
                    break;
                }
            }
        }
    }

    unsigned HuffmanEncodingTree::DecodeArray(BitStream* input,
                                              BitSize_t sizeInBits,
                                              size_t maxCharsToWrite,
                                              unsigned char* output) {
        unsigned outputWriteIndex = 0;
        HuffmanEncodingTreeNode* currentNode = root;

        for (unsigned counter = 0; counter < sizeInBits; ++counter) {
            if (!input->ReadBit())
                currentNode = currentNode->left;
            else
                currentNode = currentNode->right;

            if (currentNode->left == nullptr && currentNode->right == nullptr) {
                if (outputWriteIndex < maxCharsToWrite)
                    output[outputWriteIndex] = currentNode->value;
                outputWriteIndex++;
                currentNode = root;
            }
        }

        return outputWriteIndex;
    }

    void HuffmanEncodingTree::DecodeArray(unsigned char* input,
                                          BitSize_t sizeInBits,
                                          BitStream* output) {
        if (sizeInBits == 0)
            return;

        BitStream bitStream(input, BITS_TO_BYTES(sizeInBits), false);
        HuffmanEncodingTreeNode* currentNode = root;

        for (unsigned counter = 0; counter < sizeInBits; ++counter) {
            if (!bitStream.ReadBit())
                currentNode = currentNode->left;
            else
                currentNode = currentNode->right;

            if (currentNode->left == nullptr && currentNode->right == nullptr) {
                output->WriteBits(&currentNode->value, sizeof(char) * 8, true);
                currentNode = root;
            }
        }
    }
}
