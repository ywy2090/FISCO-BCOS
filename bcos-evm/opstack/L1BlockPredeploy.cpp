#include "bcos-evm/opstack/L1BlockPredeploy.h"

#include "bcos-evm/eth/state/hash_utils.hpp"
#include "bcos-evm/opstack/L1BlockStorage.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include <algorithm>
#include <cstring>

namespace bcos::evm
{
namespace
{
constexpr uint32_t kL1BaseFeeSelector = 0x519b4bd3;
constexpr uint32_t kBaseFeeScalarSelector = 0xc5985918;
constexpr uint32_t kBlobBaseFeeScalarSelector = 0x68d5dca6;
constexpr uint32_t kL1BlobBaseFeeSelector = 0x84189161;
constexpr uint32_t kOperatorFeeScalarSelector = 0x4d5d9a2a;
constexpr uint32_t kOperatorFeeConstantSelector = 0x16d3bc7f;
constexpr uint32_t kSetL1BlockValuesIsthmusSelector = 0x098999be;
constexpr uint32_t kNotDepositorSelector = 0x3cc50b45;

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

evmc_result applySetterIsthmus(state::State& state, evmc_message const& msg, bytesConstRef input)
{
    if (std::memcmp(msg.sender.bytes, OP_DEPOSITOR_ACCOUNT.bytes, sizeof(msg.sender.bytes)) != 0)
    {
        evmc_bytes32 reason{};
        reason.bytes[28] = static_cast<uint8_t>((kNotDepositorSelector >> 24) & 0xff);
        reason.bytes[29] = static_cast<uint8_t>((kNotDepositorSelector >> 16) & 0xff);
        reason.bytes[30] = static_cast<uint8_t>((kNotDepositorSelector >> 8) & 0xff);
        reason.bytes[31] = static_cast<uint8_t>(kNotDepositorSelector & 0xff);
        bytes output(reason.bytes + 28, reason.bytes + 32);
        return makeResult(EVMC_REVERT, msg.gas, std::move(output));
    }

    auto const parsed = parseIsthmusL1Attributes(input);
    if (!parsed.has_value())
    {
        return makeResult(EVMC_REVERT, msg.gas);
    }

    state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BASE_FEE_SLOT), parsed->l1BaseFee);
    state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_FEE_SCALARS_SLOT),
        packL1FeeScalars(parsed->baseFeeScalar, parsed->blobBaseFeeScalar));
    state.set_storage(
        OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BLOB_BASE_FEE_SLOT), parsed->l1BlobBaseFee);
    state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(OPERATOR_FEE_PARAMS_SLOT),
        packOperatorFeeParams(parsed->operatorFeeScalar, parsed->operatorFeeConstant));
    return makeResult(EVMC_SUCCESS, msg.gas);
}
}  // namespace

std::optional<evmc_result> L1BlockPredeploy::dispatch(state::State& state, evmc_message const& msg)
{
    bytesConstRef input{msg.input_data, msg.input_size};
    if (input.size() < 4)
    {
        return makeResult(EVMC_REVERT, msg.gas);
    }

    auto const selector = readSelector(input);
    switch (selector)
    {
    case kL1BaseFeeSelector:
        return successWithU256(msg.gas, state::fromEvmC(state.get_storage(OP_L1_BLOCK_PREDEPLOY,
                                            state::toEvmC(L1_BASE_FEE_SLOT))));
    case kBaseFeeScalarSelector:
        return successWithU256(msg.gas, unpackBaseFeeScalar(state.get_storage(OP_L1_BLOCK_PREDEPLOY,
                                            state::toEvmC(L1_FEE_SCALARS_SLOT))));
    case kBlobBaseFeeScalarSelector:
        return successWithU256(msg.gas,
            unpackBlobBaseFeeScalar(
                state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_FEE_SCALARS_SLOT))));
    case kL1BlobBaseFeeSelector:
        return successWithU256(msg.gas, state::fromEvmC(state.get_storage(OP_L1_BLOCK_PREDEPLOY,
                                            state::toEvmC(L1_BLOB_BASE_FEE_SLOT))));
    case kOperatorFeeScalarSelector:
        return successWithU256(msg.gas,
            unpackOperatorFeeScalar(
                state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(OPERATOR_FEE_PARAMS_SLOT))));
    case kOperatorFeeConstantSelector:
        return successWithU256(msg.gas,
            unpackOperatorFeeConstant(
                state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(OPERATOR_FEE_PARAMS_SLOT))));
    case kSetL1BlockValuesIsthmusSelector:
        return applySetterIsthmus(state, msg, input);
    default:
        return makeResult(EVMC_REVERT, msg.gas);
    }
}
}  // namespace bcos::evm
