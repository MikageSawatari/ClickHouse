#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <string_view>
#include <stdexcept>
#include <algorithm>
#include <boost/multiprecision/cpp_int.hpp>
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

    // Helper class for bit buffer operations using Boost.Multiprecision
    // We use Boost.Multiprecision instead of manual bit manipulation for:
    // 1. Better maintainability - well-tested library code
    // 2. Cleaner API - natural bit shift and logical operations
    // 3. Performance - optimized for various architectures
    // 4. Safety - automatic bounds checking and memory management
    class BitBuffer
    {
    private:
        // Use Boost's fixed-size 256-bit unsigned integer
        // This provides exactly 256 bits of storage with efficient operations
        using uint256_t = boost::multiprecision::number<
            boost::multiprecision::cpp_int_backend<256, 256, 
                boost::multiprecision::unsigned_magnitude, 
                boost::multiprecision::unchecked, void>>;
        
        uint256_t buffer{0};
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

            // Check if value can be represented in the specified bit length
            uint64_t max_value = (bits == 64) ? UINT64_MAX : ((1ULL << bits) - 1);
            if (value > max_value)
                throw Exception(ErrorCodes::BAD_ARGUMENTS, 
                    "Value {} cannot be represented in {} bits", value, bits);

            // Shift existing bits to the left and add new value
            buffer <<= bits;
            buffer |= uint256_t(value);
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

            // Extract the most significant 'bits' bits
            size_t shift_amount = bit_count - bits;
            uint256_t mask = (uint256_t(1) << bits) - 1;
            uint64_t result = static_cast<uint64_t>((buffer >> shift_amount) & mask);
            
            // Remove extracted bits from buffer
            if (shift_amount > 0)
            {
                uint256_t remaining_mask = (uint256_t(1) << shift_amount) - 1;
                buffer &= remaining_mask;
            }
            else
            {
                buffer = 0;
            }
            
            bit_count -= bits;
            
            return result;
        }
    };
}

}
