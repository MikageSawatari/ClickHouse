#include <Functions/IFunction.h>
#include <Functions/FunctionFactory.h>
#include <Functions/FunctionHelpers.h>
#include <Functions/Bit11StringImpl.h>
#include <Core/Types.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypesNumber.h>
#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeNullable.h>
#include <Columns/ColumnString.h>
#include <Columns/ColumnsNumber.h>
#include <Columns/ColumnArray.h>
#include <Columns/ColumnNullable.h>
#include <IO/WriteHelpers.h>
#include <Common/typeid_cast.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
    extern const int BAD_ARGUMENTS;
    extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
}

class FunctionDecodeBit11String : public IFunction
{
public:
    static constexpr auto name = "decodeBit11String";
    static FunctionPtr create(ContextPtr) { return std::make_shared<FunctionDecodeBit11String>(); }

    String getName() const override { return name; }

    bool isVariadic() const override { return true; }
    size_t getNumberOfArguments() const override { return 0; }
    bool useDefaultImplementationForNulls() const override { return true; }
    bool isSuitableForShortCircuitArgumentsExecution(const DataTypesWithConstInfo & /*arguments*/) const override { return true; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (arguments.size() < 2)
            throw Exception(ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH, 
                "Function {} requires at least 2 arguments", getName());

        // 最初の引数は文字列
        if (!isString(arguments[0]))
            throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT,
                "First argument of function {} must be String, got {}",
                getName(), arguments[0]->getName());

        // 残りの引数は整数（ビット長）
        for (size_t i = 1; i < arguments.size(); ++i)
        {
            if (!WhichDataType(arguments[i]).isUInt())
                throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT,
                    "Argument {} of function {} must be unsigned integer, got {}",
                    i + 1, getName(), arguments[i]->getName());
        }

        return std::make_shared<DataTypeArray>(std::make_shared<DataTypeUInt64>());
    }

    ColumnPtr executeImpl(const ColumnsWithTypeAndName & arguments, const DataTypePtr &, size_t input_rows_count) const override
    {
        const auto * string_column = checkAndGetColumn<ColumnString>(arguments[0].column.get());
        if (!string_column)
            throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT, 
                "First argument of function {} must be String column", getName());

        // ビット長を取得
        std::vector<uint64_t> bit_lengths;
        for (size_t i = 1; i < arguments.size(); ++i)
        {
            const auto & col = arguments[i].column;
            if (isColumnConst(*col))
            {
                bit_lengths.push_back(col->getUInt(0));
            }
            else
            {
                throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT,
                    "Bit length arguments must be constant");
            }
        }

        // ビット長の合計を計算
        uint64_t total_bits = 0;
        for (auto bit_length : bit_lengths)
        {
            if (bit_length == 0 || bit_length > 64)
                throw Exception(ErrorCodes::BAD_ARGUMENTS,
                    "Bit length must be between 1 and 64, got {}", bit_length);
            total_bits += bit_length;
        }

        // 11の倍数でない場合はエラー
        if (total_bits % Bit11String::bits_per_block != 0)
            throw Exception(ErrorCodes::BAD_ARGUMENTS,
                "Total bit count {} is not a multiple of 11", total_bits);

        // 結果の配列カラムを作成
        auto result_column = ColumnArray::create(ColumnUInt64::create());
        auto & result_data = assert_cast<ColumnUInt64 &>(result_column->getData()).getData();
        auto & result_offsets = result_column->getOffsets();
        result_offsets.resize(input_rows_count);

        size_t current_offset = 0;

        // 各行を処理
        for (size_t row = 0; row < input_rows_count; ++row)
        {
            const auto & encoded_string = string_column->getDataAt(row);
            
            // 文字列長が2の倍数でない場合はエラー
            if (encoded_string.size % 2 != 0)
                throw Exception(ErrorCodes::BAD_ARGUMENTS,
                    "Encoded string length {} is not even", encoded_string.size);

            // 期待される文字列長を確認
            size_t expected_chars = (total_bits / Bit11String::bits_per_block) * Bit11String::chars_per_block;
            if (encoded_string.size != expected_chars)
                throw Exception(ErrorCodes::BAD_ARGUMENTS,
                    "Encoded string length {} does not match expected length {} for {} bits",
                    encoded_string.size, expected_chars, total_bits);

            // デコード処理
            Bit11String::BitBuffer buffer;
            
            // 2文字ずつ処理して11bitブロックをデコード
            for (size_t i = 0; i < encoded_string.size; i += Bit11String::chars_per_block)
            {
                uint64_t value11 = Bit11String::decode2CharsTo11Bit(encoded_string.data + i);
                buffer.addBits(value11, Bit11String::bits_per_block);
            }

            // 指定されたビット長で値を抽出
            for (uint64_t bit_length : bit_lengths)
            {
                uint64_t value = buffer.extractBits(bit_length);
                result_data.push_back(value);
            }

            current_offset += bit_lengths.size();
            result_offsets[row] = current_offset;
        }

        return result_column;
    }
};

REGISTER_FUNCTION(DecodeBit11String)
{
    factory.registerFunction<FunctionDecodeBit11String>(
        FunctionDocumentation{
            .description = R"(
Decodes a string encoded with encodeBit11String back to the original values.

The function takes an encoded string and bit lengths, then decodes it back to an array of unsigned integers.
Each 2 characters represent an 11-bit block using charset: a-z, 0-9, A-Z.

Examples:
    decodeBit11String('aa', 11) = [0]
    decodeBit11String('h7', 5, 6) = [7, 3]
    decodeBit11String('9b', 11) = [2047]
)",
            .syntax = "decodeBit11String(encoded_string, bit_length1, bit_length2, ...)",
            .arguments = {
                {"encoded_string", "String encoded with encodeBit11String"},
                {"bit_length", "Bit length of each value to extract"}
            },
            .returned_value = "Array of decoded unsigned integers",
            .examples = {
                {"basic", "SELECT decodeBit11String('aa', 11)", "[0]"},
                {"multiple", "SELECT decodeBit11String('h7', 5, 6)", "[7, 3]"}
            },
            .category = FunctionDocumentation::Category::String
        });
}

}
