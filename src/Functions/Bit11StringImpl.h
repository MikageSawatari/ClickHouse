#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <string_view>
#include <stdexcept>
#include <algorithm>
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
    static constexpr std::string_view charset = "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static constexpr size_t charset_size = charset.size();
    static constexpr size_t bits_per_block = 11;
    static constexpr size_t chars_per_block = 2;
    static constexpr uint64_t max_11bit_value = 2047; // 2^11 - 1

    // Character to value mapping table (for decoding)
    static constexpr std::array<uint8_t, 256> createCharToValueTable()
    {
        std::array<uint8_t, 256> table{};
        for (size_t i = 0; i < 256; ++i)
            table[i] = 255; // Invalid character marked as 255

        for (size_t i = 0; i < charset.size(); ++i)
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
        // 256 bits stored as bytes for easier manipulation
        std::array<uint8_t, 32> buffer{};  // 256 bits = 32 bytes
        size_t bit_count = 0;
        static constexpr size_t max_bits = 256;

    public:
        void addBits(uint64_t value, size_t bits)
        {
            if (bits > 64)
                throw Exception(ErrorCodes::BAD_ARGUMENTS, "Bit length {} exceeds 64", bits);

            if (bit_count + bits > max_bits)
                throw Exception(ErrorCodes::BAD_ARGUMENTS, 
                    "Total bit count {} exceeds buffer capacity of {} bits", bit_count + bits, max_bits);

            // 値がビット長で表現可能か確認
            uint64_t max_value = (bits == 64) ? UINT64_MAX : ((1ULL << bits) - 1);
            if (value > max_value)
                throw Exception(ErrorCodes::BAD_ARGUMENTS, 
                    "Value {} cannot be represented in {} bits", value, bits);

            // Add bits from MSB to LSB
            for (size_t i = 0; i < bits; ++i)
            {
                size_t bit_pos = bit_count + i;
                size_t byte_idx = bit_pos / 8;
                size_t bit_in_byte = 7 - (bit_pos % 8);
                
                if ((value >> (bits - 1 - i)) & 1)
                    buffer[byte_idx] |= (1U << bit_in_byte);
                else
                    buffer[byte_idx] &= ~(1U << bit_in_byte);
            }
            
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

            return extractBits(bits_per_block);
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

            uint64_t result = 0;
            
            // Extract bits from buffer
            for (size_t i = 0; i < bits; ++i)
            {
                size_t byte_idx = i / 8;
                size_t bit_in_byte = 7 - (i % 8);
                
                if (buffer[byte_idx] & (1U << bit_in_byte))
                    result |= (1ULL << (bits - 1 - i));
            }
            
            // Shift remaining bits to the left
            size_t remaining_bits = bit_count - bits;
            for (size_t i = 0; i < remaining_bits; ++i)
            {
                size_t src_byte = (i + bits) / 8;
                size_t src_bit = 7 - ((i + bits) % 8);
                size_t dst_byte = i / 8;
                size_t dst_bit = 7 - (i % 8);
                
                if (buffer[src_byte] & (1U << src_bit))
                    buffer[dst_byte] |= (1U << dst_bit);
                else
                    buffer[dst_byte] &= ~(1U << dst_bit);
            }
            
            // Clear the tail
            size_t clear_start = (remaining_bits + 7) / 8;
            for (size_t i = clear_start; i < buffer.size(); ++i)
                buffer[i] = 0;
            
            bit_count = remaining_bits;
            
            return result;
        }
    };
}

}
