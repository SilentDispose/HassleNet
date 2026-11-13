#pragma once

#include "Types.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace SLikeNet {
#define BITS_TO_BYTES(x) (((x) + 7) >> 3)
#define BYTES_TO_BITS(x) ((x) << 3)

    static const unsigned int BITSTREAM_STACK_ALLOCATION_SIZE = 256;

    class BitStream {
    public:
        BitStream();
        explicit BitStream(unsigned int initialBytesToAllocate);
        BitStream(const unsigned char* data, unsigned int lengthInBytes, bool copyData = true);
        ~BitStream();

        BitStream(const BitStream&) = delete;
        BitStream& operator=(const BitStream&) = delete;

        void Reset();
        void ResetReadPointer() {
            readOffset = 0;
        }
        void ResetWritePointer() {
            numberOfBitsUsed = 0;
        }
        void IgnoreBits(BitSize_t numberOfBits);
        void IgnoreBytes(unsigned int numberOfBytes) {
            IgnoreBits(BYTES_TO_BITS(numberOfBytes));
        }
        void AlignWriteToByteBoundary() {
            numberOfBitsUsed += 8 - (((numberOfBitsUsed - 1) & 7) + 1);
        }
        void AlignReadToByteBoundary() {
            readOffset += 8 - (((readOffset - 1) & 7) + 1);
        }

        BitSize_t GetNumberOfBitsUsed() const {
            return numberOfBitsUsed;
        }
        BitSize_t GetWriteOffset() const {
            return numberOfBitsUsed;
        }
        BitSize_t GetReadOffset() const {
            return readOffset;
        }
        BitSize_t GetNumberOfBytesUsed() const {
            return BITS_TO_BYTES(numberOfBitsUsed);
        }
        BitSize_t GetNumberOfUnreadBits() const {
            return readOffset > numberOfBitsUsed ? 0 : numberOfBitsUsed - readOffset;
        }
        BitSize_t GetNumberOfBitsAllocated() const {
            return numberOfBitsAllocated;
        }

        void SetWriteOffset(BitSize_t offset) {
            numberOfBitsUsed = offset;
        }
        void SetReadOffset(BitSize_t offset) {
            readOffset = offset;
        }

        const unsigned char* GetData() const {
            return data;
        }
        unsigned char* GetData() {
            return data;
        }

        void WriteBits(const unsigned char* inByteArray,
                       BitSize_t numberOfBitsToWrite,
                       bool rightAlignedBits = true);
        bool ReadBits(unsigned char* inOutByteArray,
                      BitSize_t numberOfBitsToRead,
                      bool alignBitsToRight = true);
        void Write(const BitStream* bitStream, BitSize_t numberOfBits);
        bool Read(BitStream* bitStream, BitSize_t numberOfBits);
        void Write(BitStream& bitStream, BitSize_t numberOfBits) {
            Write(&bitStream, numberOfBits);
        }
        bool Read(BitStream& bitStream, BitSize_t numberOfBits) {
            return Read(&bitStream, numberOfBits);
        }

        void WriteAlignedBytes(const unsigned char* inByteArray, unsigned int numberOfBytesToWrite);
        bool ReadAlignedBytes(unsigned char* inOutByteArray, unsigned int numberOfBytesToRead);

        void Write0() {
            Write(false);
        }
        void Write1() {
            Write(true);
        }
        bool ReadBit() {
            bool b = false;
            Read(b);
            return b;
        }

        template <class T> void Write(T inTemplateVar) {
            unsigned char buf[sizeof(T)];
            ToNetworkOrder(inTemplateVar, buf);
            WriteBits(buf, (BitSize_t)(sizeof(T) << 3), true);
        }

        template <class T> bool Read(T& inOutTemplateVar) {
            unsigned char buf[sizeof(T)];
            if (!ReadBits(buf, (BitSize_t)(sizeof(T) << 3), true))
                return false;
            FromNetworkOrder(buf, inOutTemplateVar);
            return true;
        }

        template <class T> void WriteLE(T inTemplateVar) {
            unsigned char buf[sizeof(T)];
            for (unsigned i = 0; i < sizeof(T); ++i)
                buf[i] = (unsigned char)((uint64_t)inTemplateVar >> (8 * i));
            WriteBits(buf, (BitSize_t)(sizeof(T) << 3), true);
        }

        template <class T> bool ReadLE(T& inOutTemplateVar) {
            unsigned char buf[sizeof(T)];
            if (!ReadBits(buf, (BitSize_t)(sizeof(T) << 3), true))
                return false;
            uint64_t v = 0;
            for (unsigned i = 0; i < sizeof(T); ++i)
                v |= (uint64_t)buf[i] << (8 * i);
            inOutTemplateVar = (T)v;
            return true;
        }

        void WriteStringLE(const char* str, unsigned int length) {
            WriteLE<unsigned int>(length);
            if (length)
                WriteAlignedBytes((const unsigned char*)str, length);
        }
        bool ReadStringLE(std::string& out) {
            unsigned int length = 0;
            if (!ReadLE<unsigned int>(length))
                return false;
            if (length > GetNumberOfUnreadBits() / 8)
                return false;
            out.resize(length);
            if (length && !ReadAlignedBytes((unsigned char*)(&out[0]), length)) {
                out.clear();
                return false;
            }
            return true;
        }

        template <class T> BitStream& operator>>(T& v) {
            ReadLE<T>(v);
            return *this;
        }
        template <class T> BitStream& operator<<(T v) {
            WriteLE<T>(v);
            return *this;
        }
        BitStream& operator>>(std::string& s) {
            ReadStringLE(s);
            return *this;
        }
        BitStream& operator<<(const std::string& s) {
            WriteStringLE(s.data(), (unsigned int)s.size());
            return *this;
        }
        BitStream& operator<<(const char* s) {
            if (s)
                WriteStringLE(s, (unsigned int)strlen(s));
            else
                WriteLE<unsigned int>(0u);
            return *this;
        }

        void Write(bool value);
        bool Read(bool& value);
        void Write(const char* str);
        bool Read(char* out, size_t outSize);

        void WriteCompressed(unsigned char v) {
            WriteCompressedRaw(&v, 8, true);
        }
        void WriteCompressed(bool v) {
            unsigned char c = v ? 1 : 0;
            WriteCompressedRaw(&c, 8, true);
        }
        void WriteCompressed(short v) {
            unsigned char b[2];
            ToNetworkOrder(v, b);
            WriteCompressedRaw(b, 16, false);
        }
        void WriteCompressed(unsigned short v) {
            unsigned char b[2];
            ToNetworkOrder(v, b);
            WriteCompressedRaw(b, 16, true);
        }
        void WriteCompressed(int v) {
            unsigned char b[4];
            ToNetworkOrder(v, b);
            WriteCompressedRaw(b, 32, false);
        }
        void WriteCompressed(unsigned int v) {
            unsigned char b[4];
            ToNetworkOrder(v, b);
            WriteCompressedRaw(b, 32, true);
        }
        void WriteCompressed(int64_t v) {
            unsigned char b[8];
            ToNetworkOrder(v, b);
            WriteCompressedRaw(b, 64, false);
        }
        void WriteCompressed(uint64_t v) {
            unsigned char b[8];
            ToNetworkOrder(v, b);
            WriteCompressedRaw(b, 64, true);
        }

        bool ReadCompressed(unsigned char& v) {
            return ReadCompressedRaw(&v, 8, true);
        }
        bool ReadCompressed(bool& v);
        bool ReadCompressed(short& v) {
            unsigned char b[2];
            if (!ReadCompressedRaw(b, 16, false))
                return false;
            FromNetworkOrder(b, v);
            return true;
        }
        bool ReadCompressed(unsigned short& v) {
            unsigned char b[2];
            if (!ReadCompressedRaw(b, 16, true))
                return false;
            FromNetworkOrder(b, v);
            return true;
        }
        bool ReadCompressed(int& v) {
            unsigned char b[4];
            if (!ReadCompressedRaw(b, 32, false))
                return false;
            FromNetworkOrder(b, v);
            return true;
        }
        bool ReadCompressed(unsigned int& v) {
            unsigned char b[4];
            if (!ReadCompressedRaw(b, 32, true))
                return false;
            FromNetworkOrder(b, v);
            return true;
        }
        bool ReadCompressed(int64_t& v) {
            unsigned char b[8];
            if (!ReadCompressedRaw(b, 64, false))
                return false;
            FromNetworkOrder(b, v);
            return true;
        }
        bool ReadCompressed(uint64_t& v) {
            unsigned char b[8];
            if (!ReadCompressedRaw(b, 64, true))
                return false;
            FromNetworkOrder(b, v);
            return true;
        }

        void PadWithZeroToByteLength(unsigned int bytes);

        BitSize_t CopyData(unsigned char** out) const;

    private:
        void AddBitsAndReallocate(BitSize_t numberOfBitsToWrite);
        void WriteCompressedRaw(const unsigned char* inByteArray,
                                unsigned int sizeInBits,
                                bool unsignedData);
        bool ReadCompressedRaw(unsigned char* inOutByteArray,
                               unsigned int sizeInBits,
                               bool unsignedData);

        template <class T> static void ToNetworkOrder(T value, unsigned char* out) {
            for (unsigned i = 0; i < sizeof(T); ++i)
                out[i] = (unsigned char)((uint64_t)value >> (8 * (sizeof(T) - 1 - i)));
        }

        template <class T> static void FromNetworkOrder(const unsigned char* in, T& value) {
            uint64_t v = 0;
            for (unsigned i = 0; i < sizeof(T); ++i)
                v = (v << 8) | in[i];
            value = (T)v;
        }

        BitSize_t numberOfBitsUsed;
        BitSize_t numberOfBitsAllocated;
        BitSize_t readOffset;
        unsigned char* data;
        bool copyData;
        unsigned char stackData[BITSTREAM_STACK_ALLOCATION_SIZE];

        friend struct BitStreamLayoutCheck;
    };

    struct BitStreamLayoutCheck {
        static_assert(sizeof(BitStream) == 288, "BitStream ABI mismatch");
        static_assert(offsetof(BitStream, readOffset) == 8, "BitStream::readOffset offset");
        static_assert(offsetof(BitStream, data) == 16, "BitStream::data offset");
        static_assert(offsetof(BitStream, copyData) == 24, "BitStream::copyData offset");
    };
}
