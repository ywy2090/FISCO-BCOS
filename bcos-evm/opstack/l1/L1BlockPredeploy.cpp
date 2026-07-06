#include "bcos-evm/opstack/l1/L1BlockPredeploy.h"

#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/l1/L1BlockSelectors.h"
#include "bcos-evm/opstack/l1/L1BlockStorage.h"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include "bcos-utilities/Common.h"
#include <algorithm>
#include <cstring>
#include <string_view>
#include <utility>

namespace bcos::evm
{
namespace
{
/// First four bytes of calldata as big-endian uint32 (Solidity function selector).
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
        // evmc_result owns output via release callback (matches evmone / adapter conventions).
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

evmc_result successWithBytes(int64_t gasLeft, bytes output)
{
    return makeResult(EVMC_SUCCESS, gasLeft, std::move(output));
}

evmc_result applySetterIsthmus(state::State& state, evmc_message const& msg, bytesConstRef input)
{
    // Only the synthetic depositor (first system deposit tx per L2 block) may write L1 attrs.
    if (std::memcmp(msg.sender.bytes, OP_DEPOSITOR_ACCOUNT.bytes, sizeof(msg.sender.bytes)) != 0)
    {
        // Solidity custom error: revert data is abi.encodeWithSelector(NotDepositor()) → last 4
        // bytes.
        evmc_bytes32 reason{};
        reason.bytes[28] = static_cast<uint8_t>((l1block::kNotDepositor >> 24) & 0xff);
        reason.bytes[29] = static_cast<uint8_t>((l1block::kNotDepositor >> 16) & 0xff);
        reason.bytes[30] = static_cast<uint8_t>((l1block::kNotDepositor >> 8) & 0xff);
        reason.bytes[31] = static_cast<uint8_t>(l1block::kNotDepositor & 0xff);
        bytes output(reason.bytes + 28, reason.bytes + 32);
        return makeResult(EVMC_REVERT, msg.gas, std::move(output));
    }

    auto const parsed = parseIsthmusL1Attributes(input);
    if (!parsed.has_value())
    {
        return makeResult(EVMC_REVERT, msg.gas);
    }

    state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_NUMBER_TIMESTAMP_SLOT),
        packL1NumberTimestamp(parsed->timestamp, parsed->l1BlockNumber));
    state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BASE_FEE_SLOT), parsed->l1BaseFee);
    state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_HASH_SLOT), parsed->hash);
    state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_FEE_SCALARS_SLOT),
        packL1FeeScalarsSlot(
            parsed->baseFeeScalar, parsed->blobBaseFeeScalar, parsed->sequenceNumber));
    state.set_storage(
        OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BATCHER_HASH_SLOT), parsed->batcherHash);
    state.set_storage(
        OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BLOB_BASE_FEE_SLOT), parsed->l1BlobBaseFee);
    state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(OPERATOR_FEE_PARAMS_SLOT),
        packOperatorFeeParams(parsed->operatorFeeScalar, parsed->operatorFeeConstant));
    return makeResult(EVMC_SUCCESS, msg.gas);
}

/// Jovian setter: same L1 fields as Isthmus plus daFootprintGasScalar in OPERATOR_FEE_PARAMS_SLOT.
evmc_result applySetterJovian(state::State& state, evmc_message const& msg, bytesConstRef input)
{
    if (std::memcmp(msg.sender.bytes, OP_DEPOSITOR_ACCOUNT.bytes, sizeof(msg.sender.bytes)) != 0)
    {
        // Same NotDepositor() encoding as Isthmus path.
        evmc_bytes32 reason{};
        reason.bytes[28] = static_cast<uint8_t>((l1block::kNotDepositor >> 24) & 0xff);
        reason.bytes[29] = static_cast<uint8_t>((l1block::kNotDepositor >> 16) & 0xff);
        reason.bytes[30] = static_cast<uint8_t>((l1block::kNotDepositor >> 8) & 0xff);
        reason.bytes[31] = static_cast<uint8_t>(l1block::kNotDepositor & 0xff);
        bytes output(reason.bytes + 28, reason.bytes + 32);
        return makeResult(EVMC_REVERT, msg.gas, std::move(output));
    }

    auto const parsed = parseJovianL1Attributes(input);
    if (!parsed.has_value())
    {
        return makeResult(EVMC_REVERT, msg.gas);
    }

    state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_NUMBER_TIMESTAMP_SLOT),
        packL1NumberTimestamp(parsed->timestamp, parsed->l1BlockNumber));
    state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BASE_FEE_SLOT), parsed->l1BaseFee);
    state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_HASH_SLOT), parsed->hash);
    state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_FEE_SCALARS_SLOT),
        packL1FeeScalarsSlot(
            parsed->baseFeeScalar, parsed->blobBaseFeeScalar, parsed->sequenceNumber));
    state.set_storage(
        OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BATCHER_HASH_SLOT), parsed->batcherHash);
    state.set_storage(
        OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BLOB_BASE_FEE_SLOT), parsed->l1BlobBaseFee);
    state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(OPERATOR_FEE_PARAMS_SLOT),
        packOperatorFeeParams(
            parsed->operatorFeeScalar, parsed->operatorFeeConstant, parsed->daFootprintGasScalar));
    return makeResult(EVMC_SUCCESS, msg.gas);
}
}  // namespace

std::optional<evmc_result> L1BlockPredeploy::dispatchGetter(
    state::State& state, uint32_t selector, int64_t gas)
{
    switch (selector)
    {
    case l1block::kNumber:
        // Slot 0: high 8 bytes = L1 timestamp, low 8 bytes = L1 block number.
        return successWithU256(gas, unpackNumber(state.get_storage(OP_L1_BLOCK_PREDEPLOY,
                                        state::toEvmC(L1_NUMBER_TIMESTAMP_SLOT))));
    case l1block::kTimestamp:
        return successWithU256(gas, unpackTimestamp(state.get_storage(OP_L1_BLOCK_PREDEPLOY,
                                        state::toEvmC(L1_NUMBER_TIMESTAMP_SLOT))));
    case l1block::kBasefee:
    case l1block::kL1BaseFee:
        return successWithU256(gas, state::fromEvmC(state.get_storage(
                                        OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BASE_FEE_SLOT))));
    case l1block::kHash:
        return successWithU256(gas,
            state::fromEvmC(state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_HASH_SLOT))));
    case l1block::kSequenceNumber:
        // Slot 3: sequence lives in the low 8 bytes (same unpack as number in slot 0).
        return successWithU256(gas, unpackSequenceNumber(state.get_storage(OP_L1_BLOCK_PREDEPLOY,
                                        state::toEvmC(L1_FEE_SCALARS_SLOT))));
    case l1block::kBlobBaseFeeScalar:
        return successWithU256(gas, unpackBlobBaseFeeScalar(state.get_storage(OP_L1_BLOCK_PREDEPLOY,
                                        state::toEvmC(L1_FEE_SCALARS_SLOT))));
    case l1block::kBaseFeeScalar:
        return successWithU256(gas, unpackBaseFeeScalar(state.get_storage(OP_L1_BLOCK_PREDEPLOY,
                                        state::toEvmC(L1_FEE_SCALARS_SLOT))));
    case l1block::kBatcherHash:
        return successWithU256(gas, state::fromEvmC(state.get_storage(OP_L1_BLOCK_PREDEPLOY,
                                        state::toEvmC(L1_BATCHER_HASH_SLOT))));
    case l1block::kL1FeeOverhead:
        return successWithU256(gas, state::fromEvmC(state.get_storage(OP_L1_BLOCK_PREDEPLOY,
                                        state::toEvmC(L1_FEE_OVERHEAD_SLOT))));
    // Legacy Bedrock slot; no longer written after Ecotone
    case l1block::kL1FeeScalar:
        return successWithU256(gas, state::fromEvmC(state.get_storage(OP_L1_BLOCK_PREDEPLOY,
                                        state::toEvmC(L1_FEE_SCALAR_LEGACY_SLOT))));
    case l1block::kBlobBaseFee:
    case l1block::kL1BlobBaseFee:
        return successWithU256(gas, state::fromEvmC(state.get_storage(OP_L1_BLOCK_PREDEPLOY,
                                        state::toEvmC(L1_BLOB_BASE_FEE_SLOT))));
    case l1block::kOperatorFeeScalar:
        // Slot 8: operator scalar at bytes[20..24); Jovian daFootprint at bytes[18..20).
        return successWithU256(gas, unpackOperatorFeeScalar(state.get_storage(OP_L1_BLOCK_PREDEPLOY,
                                        state::toEvmC(OPERATOR_FEE_PARAMS_SLOT))));
    case l1block::kOperatorFeeConstant:
        return successWithU256(gas,
            unpackOperatorFeeConstant(
                state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(OPERATOR_FEE_PARAMS_SLOT))));
    case l1block::kDaFootprintGasScalar:
        return successWithU256(gas,
            unpackDaFootprintGasScalar(
                state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(OPERATOR_FEE_PARAMS_SLOT))));
    case l1block::kDepositorAccount:
        return successWithBytes(gas, encodeAbiAddress(OP_DEPOSITOR_ACCOUNT));
    // Default ETH gas token (non-custom-gas-token chains)
    case l1block::kIsCustomGasToken:
        return successWithU256(gas, 0);
    case l1block::kGasPayingToken:
        return successWithBytes(gas, encodeGasPayingToken());
    case l1block::kGasPayingTokenName:
        return successWithBytes(gas, encodeAbiString("Ether"));
    case l1block::kGasPayingTokenSymbol:
        return successWithBytes(gas, encodeAbiString("ETH"));
    case l1block::kVersion:
        return successWithBytes(gas, encodeAbiString("1.9.0"));
    default:
        return std::nullopt;
    }
}

std::optional<evmc_result> L1BlockPredeploy::dispatch(state::State& state, evmc_message const& msg)
{
    bytesConstRef input{msg.input_data, msg.input_size};
    if (input.size() < 4)
    {
        return makeResult(EVMC_REVERT, msg.gas);
    }

    auto const selector = readSelector(input);
    // Try read-only getters first; fall through to setters / isFeatureEnabled.
    if (auto getter = dispatchGetter(state, selector, msg.gas))
    {
        return getter;
    }

    switch (selector)
    {
    case l1block::kIsFeatureEnabled:
    {
        // isFeatureEnabled(bytes32 key): single 32-byte arg after selector.
        if (input.size() < 36)
        {
            return makeResult(EVMC_REVERT, msg.gas);
        }
        evmc_bytes32 key{};
        std::memcpy(key.bytes, input.data() + 4, 32);
        return successWithU256(msg.gas, readFeatureEnabled(state, key) ? 1 : 0);
    }
    case l1block::kSetL1BlockValuesIsthmus:
        return applySetterIsthmus(state, msg, input);
    case l1block::kSetL1BlockValuesJovian:
        // Active once Jovian fork is live; deposit tx uses the longer calldata layout.
        return applySetterJovian(state, msg, input);
    default:
        return makeResult(EVMC_REVERT, msg.gas);
    }
}
}  // namespace bcos::evm
