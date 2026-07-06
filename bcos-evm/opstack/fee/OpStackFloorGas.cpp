#include "bcos-evm/opstack/fee/OpStackFloorGas.h"

#include "bcos-utilities/Common.h"
#include <limits>

namespace bcos::evm
{
namespace
{
uint64_t countZeroBytes(bcos::bytesConstRef data)
{
    uint64_t zeroes = 0;
    for (auto const byte : data)
    {
        if (byte == 0x00)
        {
            ++zeroes;
        }
    }
    return zeroes;
}

FloorDataGasResult floorDataGasFromTokens(uint64_t tokens)
{
    if ((std::numeric_limits<uint64_t>::max() - OP_TX_GAS) / OP_TX_COST_FLOOR_PER_TOKEN < tokens)
    {
        return FloorDataGasResult{.gas = 0, .error = FloorDataGasError::GasUintOverflow};
    }
    return FloorDataGasResult{.gas = OP_TX_GAS + tokens * OP_TX_COST_FLOOR_PER_TOKEN, .error = {}};
}
}  // namespace

FloorDataGasResult tryFloorDataGas(bcos::bytesConstRef data, std::optional<uint64_t> tokenOverride)
{
    auto const dataLen = static_cast<uint64_t>(data.size());
    auto const zeroes = countZeroBytes(data);
    auto const nonZeroes = dataLen - zeroes;
    auto const tokens = tokenOverride.has_value() ?
                            *tokenOverride :
                            nonZeroes * OP_TX_TOKEN_PER_NON_ZERO_BYTE + zeroes;
    return floorDataGasFromTokens(tokens);
}

uint64_t floorDataGas(bcos::bytesConstRef data)
{
    return tryFloorDataGas(data).gas;
}

ExecuteEntryFloorCheck executeEntryFloorDataGasCheck(uint64_t gasLimit, bcos::bytesConstRef data)
{
    auto const floorResult = tryFloorDataGas(data);
    if (floorResult.error.has_value())
    {
        return ExecuteEntryFloorCheck{
            .ok = false,
            .error = ExecuteEntryFloorError::GasUintOverflow,
            .floorGas = 0,
            .gasLimit = gasLimit,
        };
    }

    auto const floorGasValue = floorResult.gas;
    if (gasLimit < floorGasValue)
    {
        return ExecuteEntryFloorCheck{
            .ok = false,
            .error = ExecuteEntryFloorError::BelowFloor,
            .floorGas = floorGasValue,
            .gasLimit = gasLimit,
        };
    }

    return ExecuteEntryFloorCheck{
        .ok = true,
        .error = {},
        .floorGas = floorGasValue,
        .gasLimit = gasLimit,
    };
}

}  // namespace bcos::evm
