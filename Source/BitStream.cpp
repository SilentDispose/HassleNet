#include "HassleNet/SLikeNet/BitStream.h"

#include <cstdlib>
#include <cstring>

namespace SLikeNet {
    BitStream::BitStream()
        : numberOfBitsUsed(0)
        , numberOfBitsAllocated(BITSTREAM_STACK_ALLOCATION_SIZE * 8)
        , readOffset(0)
        , data(stackData)
        , copyData(true) {}

    BitStream::BitStream(unsigned int initialBytesToAllocate)
        : numberOfBitsUsed(0)
        , numberOfBitsAllocated(BITSTREAM_STACK_ALLOCATION_SIZE * 8)
        , readOffset(0)
        , data(stackData)
        , copyData(true) {
        if (initialBytesToAllocate > BITSTREAM_STACK_ALLOCATION_SIZE) {
            data = (unsigned char*)malloc(initialBytesToAllocate);
            numberOfBitsAllocated = initialBytesToAllocate << 3;
        }
    }

    BitStream::BitStream(const unsigned char* inData, unsigned int lengthInBytes, bool doCopy)
        : numberOfBitsUsed(lengthInBytes << 3)
        , numberOfBitsAllocated(lengthInBytes << 3)
        , readOffset(0)
        , data(nullptr)
        , copyData(doCopy) {
        if (copyData) {
            if (lengthInBytes > 0) {
                if (lengthInBytes < BITSTREAM_STACK_ALLOCATION_SIZE) {
                    data = stackData;
                    numberOfBitsAllocated = BITSTREAM_STACK_ALLOCATION_SIZE << 3;
                } else {
                    data = (unsigned char*)malloc(lengthInBytes);
                }
                memcpy(data, inData, lengthInBytes);
            }
        } else {
            data = const_cast<unsigned char*>(inData);
        }
    }

    BitStream::~BitStream() {
        if (copyData && numberOfBitsAllocated > (BITSTREAM_STACK_ALLOCATION_SIZE << 3) && data)
            free(data);
    }

    void BitStream::Reset() {
        numberOfBitsUsed = 0;
        readOffset = 0;
    }

    void BitStream::IgnoreBits(BitSize_t numberOfBits) {
        readOffset += numberOfBits;
    }

    void BitStream::WriteBits(const unsigned char* inByteArray,
                              BitSize_t numberOfBitsToWrite,
                              bool rightAlignedBits) {
        AddBitsAndReallocate(numberOfBitsToWrite);

        const BitSize_t numberOfBitsUsedMod8 = numberOfBitsUsed & 7;

        if (numberOfBitsUsedMod8 == 0 && (numberOfBitsToWrite & 7) == 0) {
            memcpy(data + (numberOfBitsUsed >> 3), inByteArray, numberOfBitsToWrite >> 3);
            numberOfBitsUsed += numberOfBitsToWrite;
            return;
        }

        unsigned char dataByte;
        const unsigned char* inputPtr = inByteArray;

        while (numberOfBitsToWrite > 0) {
            dataByte = *(inputPtr++);

            if (numberOfBitsToWrite < 8 && rightAlignedBits)
                dataByte <<= 8 - numberOfBitsToWrite;

            if (numberOfBitsUsedMod8 == 0) {
                *(data + (numberOfBitsUsed >> 3)) = dataByte;
            } else {
                *(data + (numberOfBitsUsed >> 3)) |= dataByte >> numberOfBitsUsedMod8;

                if (8 - (numberOfBitsUsedMod8) < 8 &&
                    8 - (numberOfBitsUsedMod8) < numberOfBitsToWrite)
                    *(data + (numberOfBitsUsed >> 3) + 1) =
                        (unsigned char)(dataByte << (8 - numberOfBitsUsedMod8));
            }

            if (numberOfBitsToWrite >= 8) {
                numberOfBitsUsed += 8;
                numberOfBitsToWrite -= 8;
            } else {
                numberOfBitsUsed += numberOfBitsToWrite;
                numberOfBitsToWrite = 0;
            }
        }
    }

    bool BitStream::ReadBits(unsigned char* inOutByteArray,
                             BitSize_t numberOfBitsToRead,
                             bool alignBitsToRight) {
        if (numberOfBitsToRead == 0)
            return false;
        if (GetNumberOfUnreadBits() < numberOfBitsToRead)
            return false;

        const BitSize_t readOffsetMod8 = readOffset & 7;

        if (readOffsetMod8 == 0 && (numberOfBitsToRead & 7) == 0) {
            memcpy(inOutByteArray, data + (readOffset >> 3), numberOfBitsToRead >> 3);
            readOffset += numberOfBitsToRead;
            return true;
        }

        BitSize_t offset = 0;
        memset(inOutByteArray, 0, (size_t)BITS_TO_BYTES(numberOfBitsToRead));

        while (numberOfBitsToRead > 0) {
            *(inOutByteArray + offset) |= *(data + (readOffset >> 3)) << (readOffsetMod8);

            if (readOffsetMod8 > 0 && numberOfBitsToRead > 8 - (readOffsetMod8))
                *(inOutByteArray + offset) |=
                    *(data + (readOffset >> 3) + 1) >> (8 - (readOffsetMod8));

            if (numberOfBitsToRead >= 8) {
                numberOfBitsToRead -= 8;
                readOffset += 8;
                offset++;
            } else {
                int neg = (int)numberOfBitsToRead - 8;
                if (neg < 0) {
                    if (alignBitsToRight)
                        *(inOutByteArray + offset) >>= -neg;
                    readOffset += 8 + neg;
                } else {
                    readOffset += 8;
                }
                offset++;
                numberOfBitsToRead = 0;
            }
        }
        return true;
    }

    void BitStream::Write(const BitStream* bitStream, BitSize_t numberOfBits) {
        if (!bitStream || numberOfBits == 0)
            return;
        const BitSize_t copyOffset = bitStream->readOffset;
        const_cast<BitStream*>(bitStream)->readOffset = 0;
        const BitSize_t bitsToCopy =
            numberOfBits > bitStream->numberOfBitsUsed ? bitStream->numberOfBitsUsed : numberOfBits;
        WriteBits(bitStream->data, bitsToCopy, true);
        const_cast<BitStream*>(bitStream)->readOffset = copyOffset;
    }

    bool BitStream::Read(BitStream* bitStream, BitSize_t numberOfBits) {
        if (!bitStream || numberOfBits == 0)
            return false;
        if (GetNumberOfUnreadBits() < numberOfBits)
            return false;
        unsigned char temp[BITSTREAM_STACK_ALLOCATION_SIZE];
        unsigned char* buf = temp;
        const BitSize_t bytes = BITS_TO_BYTES(numberOfBits);
        if (bytes > sizeof(temp))
            buf = (unsigned char*)malloc(bytes);
        const bool ok = ReadBits(buf, numberOfBits, true);
        if (ok)
            bitStream->WriteBits(buf, numberOfBits, true);
        if (buf != temp)
            free(buf);
        return ok;
    }

    void BitStream::WriteAlignedBytes(const unsigned char* inByteArray,
                                      unsigned int numberOfBytesToWrite) {
        AlignWriteToByteBoundary();
        WriteBits(inByteArray, BYTES_TO_BITS(numberOfBytesToWrite), true);
    }

    bool BitStream::ReadAlignedBytes(unsigned char* inOutByteArray,
                                     unsigned int numberOfBytesToRead) {
        AlignReadToByteBoundary();
        return ReadBits(inOutByteArray, BYTES_TO_BITS(numberOfBytesToRead), true);
    }

    void BitStream::Write(bool value) {
        unsigned char c = value ? 1 : 0;
        WriteBits(&c, 1, true);
    }

    bool BitStream::Read(bool& value) {
        unsigned char c = 0;
        if (!ReadBits(&c, 1, true))
            return false;
        value = (c & 1) != 0;
        return true;
    }

    void BitStream::Write(const char* str) {
        unsigned short length = str ? (unsigned short)strlen(str) : 0;
        Write(length);
        if (length > 0)
            WriteAlignedBytes((const unsigned char*)str, length);
    }

    bool BitStream::Read(char* out, size_t outSize) {
        unsigned short length = 0;
        if (!Read(length))
            return false;
        if (outSize > 0) {
            size_t toRead = length < outSize - 1 ? length : outSize - 1;
            if (toRead > 0 && !ReadAlignedBytes((unsigned char*)out, (unsigned int)toRead))
                return false;
            out[toRead] = 0;
        }
        if (length > outSize - 1)
            IgnoreBits(BYTES_TO_BITS(length - (unsigned int)(outSize - 1)));
        return true;
    }

    void BitStream::WriteCompressedRaw(const unsigned char* inByteArray,
                                       unsigned int sizeInBits,
                                       bool unsignedData) {
        BitSize_t currentByte = (sizeInBits >> 3) - 1;

        unsigned char byteMatch = unsignedData ? 0 : 0xFF;

        while (currentByte > 0) {
            if (inByteArray[currentByte] == byteMatch) {
                Write(true);
            } else {
                Write(false);
                WriteBits(inByteArray, (unsigned int)((currentByte + 1) << 3), true);
                return;
            }
            currentByte--;
        }

        if ((unsignedData && ((*(inByteArray + currentByte)) & 0xF0) == 0x00) ||
            (unsignedData == false && ((*(inByteArray + currentByte)) & 0xF0) == 0xF0)) {
            Write(true);
            WriteBits(inByteArray + currentByte, 4, true);
        } else {
            Write(false);
            WriteBits(inByteArray + currentByte, 8, true);
        }
    }

    bool BitStream::ReadCompressedRaw(unsigned char* inOutByteArray,
                                      unsigned int sizeInBits,
                                      bool unsignedData) {
        unsigned int currentByte = (sizeInBits >> 3) - 1;

        unsigned char byteMatch = unsignedData ? 0 : 0xFF;
        unsigned char halfByteMatch = unsignedData ? 0 : 0xF0;

        while (currentByte > 0) {
            bool b = false;
            if (!Read(b))
                return false;
            if (b) {
                inOutByteArray[currentByte] = byteMatch;
                currentByte--;
            } else {
                if (!ReadBits(inOutByteArray, (unsigned int)((currentByte + 1) << 3)))
                    return false;
                return true;
            }
        }

        if (readOffset + 1 > numberOfBitsUsed)
            return false;

        bool b = false;
        if (!Read(b))
            return false;

        if (b) {
            if (!ReadBits(inOutByteArray + currentByte, 4))
                return false;
            inOutByteArray[currentByte] |= halfByteMatch;
        } else {
            if (!ReadBits(inOutByteArray + currentByte, 8))
                return false;
        }
        return true;
    }

    bool BitStream::ReadCompressed(bool& value) {
        unsigned char c = 0;
        if (!ReadCompressedRaw(&c, 8, true))
            return false;
        value = c != 0;
        return true;
    }

    void BitStream::AddBitsAndReallocate(BitSize_t numberOfBitsToWrite) {
        BitSize_t newNumberOfBitsAllocated = numberOfBitsToWrite + numberOfBitsUsed;

        if (numberOfBitsToWrite + numberOfBitsUsed > 0 &&
            ((numberOfBitsAllocated - 1) >> 3) < ((newNumberOfBitsAllocated - 1) >> 3)) {
            newNumberOfBitsAllocated = (numberOfBitsToWrite + numberOfBitsUsed) * 2;
            if (newNumberOfBitsAllocated - (numberOfBitsToWrite + numberOfBitsUsed) > 1048576)
                newNumberOfBitsAllocated = numberOfBitsToWrite + numberOfBitsUsed + 1048576;

            BitSize_t amountToAllocate = BITS_TO_BYTES(newNumberOfBitsAllocated);
            if (data == stackData) {
                if (amountToAllocate > BITSTREAM_STACK_ALLOCATION_SIZE) {
                    data = (unsigned char*)malloc((size_t)amountToAllocate);
                    memcpy(data, stackData, (size_t)BITS_TO_BYTES(numberOfBitsAllocated));
                }
            } else {
                data = (unsigned char*)realloc(data, (size_t)amountToAllocate);
            }
        }

        if (newNumberOfBitsAllocated > numberOfBitsAllocated)
            numberOfBitsAllocated = newNumberOfBitsAllocated;
    }

    BitSize_t BitStream::CopyData(unsigned char** out) const {
        const BitSize_t bytes = GetNumberOfBytesUsed();
        *out = (unsigned char*)malloc(bytes > 0 ? bytes : 1);
        if (numberOfBitsUsed > 0)
            memcpy(*out, data, bytes);
        return numberOfBitsUsed;
    }

    void BitStream::PadWithZeroToByteLength(unsigned int bytes) {
        if (GetNumberOfBytesUsed() < bytes) {
            AlignWriteToByteBoundary();
            unsigned int numToWrite = bytes - GetNumberOfBytesUsed();
            AddBitsAndReallocate(BYTES_TO_BITS(numToWrite));
            memset(data + BITS_TO_BYTES(numberOfBitsUsed), 0, (size_t)numToWrite);
            numberOfBitsUsed += BYTES_TO_BITS(numToWrite);
        }
    }
}
