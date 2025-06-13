#include <Functions/IFunction.h>
#include <Functions/FunctionFactory.h>
#include <Functions/FunctionHelpers.h>
#include <Functions/Bit11StringImpl.h>
#include <Core/Types.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypesNumber.h>
#include <DataTypes/DataTypeNullable.h>
#include <Columns/ColumnString.h>
#include <Columns/ColumnsNumber.h>
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

class FunctionEncodeBit11String : public IFunction
{
public:
    static constexpr auto name = "encodeBit11String";
    static FunctionPtr create(ContextPtr) { return std::make_shared<FunctionEncodeBit11String>(); }

    String getName() const override { return name; }

    bool isVariadic() const override { return true; }
    size_t getNumberOfArguments() const override { return 0; }
    bool useDefaultImplementationForNulls() const override { return true; }
    bool isSuitableForShortCircuitArgumentsExecution(const DataTypesWithConstInfo & /*arguments*/) const override { return true; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (arguments.empty())
            throw Exception(ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH, 
                "Function {} requires at least 2 arguments", getName());

        if (arguments.size() % 2 != 0)
            throw Exception(ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH, 
                "Function {} requires an even number of arguments", getName());

        // すべての引数が整数型であることを確認
        for (size_t i = 0; i < arguments.size(); ++i)
        {
            if (!WhichDataType(arguments[i]).isUInt())
                throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT,
                    "Argument {} of function {} must be unsigned integer, got {}",
                    i + 1, getName(), arguments[i]->getName());
        }

        return std::make_shared<DataTypeString>();
    }

    ColumnPtr executeImpl(const ColumnsWithTypeAndName & arguments, const DataTypePtr &, size_t input_rows_count) const override
    {
        auto result_column = ColumnString::create();
        auto & result_data = result_column->getChars();
        auto & result_offsets = result_column->getOffsets();
        result_offsets.resize(input_rows_count);

        // 各行を処理
        for (size_t row = 0; row < input_rows_count; ++row)
        {
            Bit11String::BitBuffer buffer;
            
            // 引数のペアを処理（ビット長、値）
            for (size_t i = 0; i < arguments.size(); i += 2)
            {
                const auto & bit_length_column = arguments[i].column;
                const auto & value_column = arguments[i + 1].column;

                uint64_t bit_length = bit_length_column->getUInt(row);
                uint64_t value = value_column->getUInt(row);

                if (bit_length == 0 || bit_length > 64)
                    throw Exception(ErrorCodes::BAD_ARGUMENTS,
                        "Bit length must be between 1 and 64, got {}", bit_length);

                buffer.addBits(value, bit_length);
            }

            // バッファのビット数が11の倍数でない場合はエラー
            if (buffer.getBitCount() % Bit11String::bits_per_block != 0)
                throw Exception(ErrorCodes::BAD_ARGUMENTS,
                    "Total bit count {} is not a multiple of 11", buffer.getBitCount());

            // 11bitずつエンコード
            size_t output_size = (buffer.getBitCount() / Bit11String::bits_per_block) * Bit11String::chars_per_block;
            size_t old_size = result_data.size();
            result_data.resize(old_size + output_size + 1); // +1 for null terminator

            char* output_ptr = reinterpret_cast<char*>(&result_data[old_size]);
            size_t output_pos = 0;

            while (buffer.canExtract11Bits())
            {
                uint64_t value11 = buffer.extract11Bits();
                Bit11String::encode11BitTo2Chars(value11, output_ptr + output_pos);
                output_pos += Bit11String::chars_per_block;
            }

            result_data[old_size + output_size] = '\0';
            result_offsets[row] = old_size + output_size + 1;
        }

        return result_column;
    }
};

REGISTER_FUNCTION(EncodeBit11String)
{
    factory.registerFunction<FunctionEncodeBit11String>(
        FunctionDocumentation{
            .description = R"(
Encodes values into a string using 11-bit blocks mapped to 2 characters each.

The function takes pairs of arguments (bit_length, value) and combines them into a bit stream,
then encodes each 11-bit block as 2 characters using charset: a-z, 0-9, A-Z.

Examples:
    encodeBit11String(11, 0) = 'aa'
    encodeBit11String(5, 7, 6, 3) = 'h7'
    encodeBit11String(11, 2047) = '9b'
)",
            .syntax = "encodeBit11String(bit_length1, value1, bit_length2, value2, ...)",
            .arguments = {
                {"bit_length", "Bit length of the value (1-64)"},
                {"value", "Unsigned integer value to encode"}
            },
            .returned_value = "Encoded string",
            .examples = {
                {"basic", "SELECT encodeBit11String(11, 0)", "'aa'"},
                {"multiple", "SELECT encodeBit11String(5, 7, 6, 3)", "'h7'"}
            },
            .category = FunctionDocumentation::Category::String
        });
}

}
