#include "bcos-evm/opstack/GasPriceOraclePredeploy.h"

#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/GasPriceOracleSelectors.h"
#include "bcos-evm/opstack/L1BlockPredeploy.h"
#include "bcos-evm/opstack/L1BlockSelectors.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackFee.h"
#include "bcos-evm/opstack/RollupCost.h"
#include <algorithm>
#include <cstring>

namespace bcos::evm
{
namespace
{
uint32_t readSelector(bytesConstRef input)
{
    return (static_cast<uint32_t>(input[0]) << 24) | (static_cast<uint32_t>(input[1]) << 16) |
           (static_cast<uint32_t>(input[2]) << 8) | static_cast<uint32_t>(input[3]);
}

evmc_result makeResult(evmc_status_code status, int64_t gasLeft, bytes output = {})
{
    evmc_result result{};
    result.status_code = status;
    result.gas_left = gasLeft;
    if (!output.empty())
    {
        auto* data = new uint8_t[output.size()];
        std::copy(output.begin(), output.end(), data);
        result.output_data = data;
        result.output_size = output.size();
        result.release = [](const evmc_result* value) { delete[] value->output_data; };
    }
    return result;
}

evmc_result successWithU256(int64_t gasLeft, u256 value)
{
    auto const encoded = state::toEvmC(value);
    bytes output(encoded.bytes, encoded.bytes + sizeof(encoded.bytes));
    return makeResult(EVMC_SUCCESS, gasLeft, std::move(output));
}

u256 readU256At(bytesConstRef input, size_t offset)
{
    evmc_bytes32 raw{};
    std::memcpy(raw.bytes, input.data() + offset, sizeof(raw.bytes));
    return state::fromEvmC(raw);
}

std::optional<bytesConstRef> decodeAbiBytes(bytesConstRef input)
{
    if (input.size() < 68)
    {
        return std::nullopt;
    }
    if (readU256At(input, 4) != 32)
    {
        return std::nullopt;
    }
    auto const length = readU256At(input, 36);
    if (length > input.size() - 68)
    {
        return std::nullopt;
    }
    return bytesConstRef(input.data() + 68, static_cast<size_t>(length));
}

u256 fjordLinearRegression(uint64_t fastLzSize)
{
    s256 estimatedSize = s256(L1_COST_INTERCEPT) + s256(L1_COST_FASTLZ_COEF) * s256(fastLzSize);
    if (estimatedSize < s256(MIN_TX_SIZE_SCALED))
    {
        estimatedSize = s256(MIN_TX_SIZE_SCALED);
    }
    return u256(estimatedSize);
}

std::optional<evmc_result> dispatchL1BlockProxyGetter(
    state::State& state, uint32_t gpoSelector, int64_t gas)
{
    uint32_t l1Selector = 0;
    switch (gpoSelector)
    {
    case gpo::kL1BaseFee:
        l1Selector = l1block::kL1BaseFee;
        break;
    case gpo::kBaseFeeScalar:
        l1Selector = l1block::kBaseFeeScalar;
        break;
    case gpo::kBlobBaseFeeScalar:
        l1Selector = l1block::kBlobBaseFeeScalar;
        break;
    case gpo::kBlobBaseFee:
        l1Selector = l1block::kBlobBaseFee;
        break;
    default:
        return std::nullopt;
    }
    return L1BlockPredeploy::dispatchGetter(state, l1Selector, gas);
}
}  // namespace

std::optional<evmc_result> GasPriceOraclePredeploy::dispatch(
    state::State& state, evmc_message const& msg, bcos::u256 l2BaseFee)
{
    bytesConstRef input{msg.input_data, msg.input_size};
    if (input.size() < 4)
    {
        return makeResult(EVMC_REVERT, msg.gas);
    }

    auto const selector = readSelector(input);
    if (auto proxy = dispatchL1BlockProxyGetter(state, selector, msg.gas))
    {
        return proxy;
    }

    switch (selector)
    {
    case gpo::kGasPrice:
    case gpo::kBaseFee:
        return successWithU256(msg.gas, l2BaseFee);
    case gpo::kDecimals:
        return successWithU256(msg.gas, 6);
    case gpo::kIsEcotone:
    case gpo::kIsFjord:
    case gpo::kIsIsthmus:
        return successWithU256(msg.gas, 1);
    case gpo::kIsJovian:
        return successWithU256(msg.gas, 0);
    case gpo::kOverhead:
    case gpo::kScalar:
        return makeResult(EVMC_REVERT, msg.gas);
    case gpo::kGetL1Fee:
    {
        auto const txBytes = decodeAbiBytes(input);
        if (!txBytes.has_value())
        {
            return makeResult(EVMC_REVERT, msg.gas);
        }
        auto const fee = l1CostFjord(newRollupCostData(*txBytes), loadOpStackFeeParams(state));
        return successWithU256(msg.gas, fee);
    }
    case gpo::kGetL1GasUsed:
    {
        auto const txBytes = decodeAbiBytes(input);
        if (!txBytes.has_value())
        {
            return makeResult(EVMC_REVERT, msg.gas);
        }
        auto const fastLzSize = static_cast<uint64_t>(flzCompressLen(*txBytes)) + 68;
        auto const gasUsed = fjordLinearRegression(fastLzSize) * 16 / 1'000'000;
        return successWithU256(msg.gas, gasUsed);
    }
    case gpo::kGetOperatorFee:
    {
        if (input.size() < 36)
        {
            return makeResult(EVMC_REVERT, msg.gas);
        }
        auto const gasUsed = readU256At(input, 4);
        auto const fee =
            operatorCostIsthmus(static_cast<uint64_t>(gasUsed), loadOpStackFeeParams(state));
        return successWithU256(msg.gas, fee);
    }
    default:
        return makeResult(EVMC_REVERT, msg.gas);
    }
}
}  // namespace bcos::evm
