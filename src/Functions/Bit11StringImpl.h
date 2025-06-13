#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <stdexcept>
#include <Common/Exception.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int BAD_ARGUMENTS;
}

namespace Bit11String
{
    // Character set: a-z, 0-9, A-Z (62 characters)
    static constexpr char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static constexpr size_t charset_size = 62;
    static constexpr size_t bits_per_block = 11;
    static constexpr size_t chars_per_block = 2;
    static constexpr uint64_t max_11bit_value = 2047; // 2^11 - 1

    // Character to value mapping table (for decoding)
    static constexpr std::array<uint8_t, 256> createCharToValueTable()
    {
        std::array<uint8_t, 256> table{};
        for (size_t i = 0; i < 256; ++i)
            table[i] = 255; // Invalid character marked as 255

        for (size_t i = 0; i < charset_size; ++i)
            table[static_cast<uint8_t>(charset[i])] = i;

        return table;
    }

    static constexpr auto char_to_value = createCharToValueTable();

    // Encode 11-bit value to 2 characters
    inline void encode11BitTo2Chars(uint64_t value, char* output)
    {
        if (value > max_11bit_value)
            throw Exception(ErrorCodes::BAD_ARGUMENTS, 
                "Value {} exceeds maximum 11-bit value {}", value, max_11bit_value);

        output[0] = charset[value / charset_size];
        output[1] = charset[value % charset_size];
    }

    // Decode 2 characters to 11-bit value
    inline uint64_t decode2CharsTo11Bit(const char* input)
    {
        uint8_t val1 = char_to_value[static_cast<uint8_t>(input[0])];
        uint8_t val2 = char_to_value[static_cast<uint8_t>(input[1])];

        if (val1 == 255 || val2 == 255)
            throw Exception(ErrorCodes::BAD_ARGUMENTS, 
                "Invalid character in input: '{}{}'", input[0], input[1]);

        uint64_t result = static_cast<uint64_t>(val1) * charset_size + val2;
        
        if (result > max_11bit_value)
            throw Exception(ErrorCodes::BAD_ARGUMENTS, 
                "Decoded value {} exceeds maximum 11-bit value {}", result, max_11bit_value);

        return result;
    }

    // Helper class for bit buffer operations
    class BitBuffer
    {
    private:
        uint64_t buffer = 0;
        size_t bit_count = 0;
        static constexpr size_t max_bits = 64;

    public:
        void addBits(uint64_t value, size_t bits)
        {
            if (bits > 64)
                throw Exception(ErrorCodes::BAD_ARGUMENTS, "Bit length {} exceeds 64", bits);

            if (bit_count + bits > max_bits)
                throw Exception(ErrorCodes::BAD_ARGUMENTS, 
                    "Total bit count {} exceeds buffer capacity", bit_count + bits);

            // 値がビット長で表現可能か確認
            uint64_t max_value = (bits == 64) ? UINT64_MAX : ((1ULL << bits) - 1);
            if (value > max_value)
                throw Exception(ErrorCodes::BAD_ARGUMENTS, 
                    "Value {} cannot be represented in {} bits", value, bits);

            buffer = (buffer << bits) | value;
            bit_count += bits;
        }

        bool canExtract11Bits() const
        {
            return bit_count >= bits_per_block;
        }

        uint64_t extract11Bits()
        {
            if (bit_count < bits_per_block)
                throw Exception(ErrorCodes::BAD_ARGUMENTS, 
                    "Not enough bits in buffer: {} < {}", bit_count, bits_per_block);

            bit_count -= bits_per_block;
            uint64_t result = (buffer >> bit_count) & max_11bit_value;
            buffer &= (1ULL << bit_count) - 1; // Clear used bits
            return result;
        }

        size_t getBitCount() const { return bit_count; }
        
        bool isEmpty() const { return bit_count == 0; }

        // For decoding: extract specified number of bits
        uint64_t extractBits(size_t bits)
        {
            if (bits > bit_count)
                throw Exception(ErrorCodes::BAD_ARGUMENTS, 
                    "Requested {} bits but only {} available", bits, bit_count);

            if (bits > 64)
                throw Exception(ErrorCodes::BAD_ARGUMENTS, "Bit length {} exceeds 64", bits);

            size_t shift = bit_count - bits;
            uint64_t mask = (bits == 64) ? UINT64_MAX : ((1ULL << bits) - 1);
            uint64_t result = (buffer >> shift) & mask;
            
            buffer &= (1ULL << shift) - 1;
            bit_count = shift;
            
            return result;
        }
    };
}

}
