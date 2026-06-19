#include "bcos-evm/opstack/OpStackFee.h"

#include "bcos-evm/eth/state/hash_utils.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"

namespace bcos::evm
{
namespace
{
constexpr size_t kScalarSectionStart = 32 - 12 - 4;

u256 readBigEndianU32(evmc_bytes32 const& slot, size_t offset)
{
    return u256((static_cast<uint64_t>(slot.bytes[offset]) << 24) |
                (static_cast<uint64_t>(slot.bytes[offset + 1]) << 16) |
                (static_cast<uint64_t>(slot.bytes[offset + 2]) << 8) |
                static_cast<uint64_t>(slot.bytes[offset + 3]));
}

u256 readBigEndianU64(evmc_bytes32 const& slot, size_t offset)
{
    u256 value = 0;
    for (size_t i = 0; i < 8; ++i)
    {
        value = (value << 8) | slot.bytes[offset + i];
    }
    return value;
}

evmc_bytes32 slotKey(u256 slot)
{
    return state::toEvmC(slot);
}
}  // namespace

u256 l1CostFjord(RollupCostData const& data, OpStackFeeParams const& params)
{
    if (data.isEmpty())
    {
        return 0;
    }

    auto const scaledL1BaseFee = params.l1BaseFeeScalar * params.l1BaseFee;
    auto const calldataCostPerByte = scaledL1BaseFee * u256(16);
    auto const blobCostPerByte = params.l1BlobBaseFeeScalar * params.l1BlobBaseFee;
    auto const l1FeeScaled = calldataCostPerByte + blobCostPerByte;

    s256 estimatedSize =
        s256(L1_COST_INTERCEPT) + s256(L1_COST_FASTLZ_COEF) * s256(data.fastLzSize);
    if (estimatedSize < s256(MIN_TX_SIZE_SCALED))
    {
        estimatedSize = s256(MIN_TX_SIZE_SCALED);
    }

    return u256(estimatedSize) * l1FeeScaled / u256(FJORD_DIVISOR);
}

u256 operatorCostIsthmus(uint64_t gas, OpStackFeeParams const& params)
{
    if (params.operatorFeeScalar == 0 && params.operatorFeeConstant == 0)
    {
        return 0;
    }

    auto fee = u256(gas) * params.operatorFeeScalar / u256(1'000'000);
    fee += params.operatorFeeConstant;
    return fee;
}

OpStackFeeParams loadOpStackFeeParams(state::StateView const& state)
{
    OpStackFeeParams params{};

    auto const readSlot = [&](u256 slot) -> evmc_bytes32 {
        return state.get_storage(OP_L1_BLOCK_PREDEPLOY, slotKey(slot));
    };

    params.l1BaseFee = state::fromEvmC(readSlot(L1_BASE_FEE_SLOT));
    params.l1BlobBaseFee = state::fromEvmC(readSlot(L1_BLOB_BASE_FEE_SLOT));

    auto const feeScalars = readSlot(L1_FEE_SCALARS_SLOT);
    params.l1BaseFeeScalar = readBigEndianU32(feeScalars, kScalarSectionStart);
    params.l1BlobBaseFeeScalar = readBigEndianU32(feeScalars, kScalarSectionStart + 4);

    auto const operatorFeeParams = readSlot(OPERATOR_FEE_PARAMS_SLOT);
    if (state::isZeroBytes32(operatorFeeParams))
    {
        return params;
    }

    params.operatorFeeScalar = readBigEndianU32(operatorFeeParams, 20);
    params.operatorFeeConstant = readBigEndianU64(operatorFeeParams, 24);
    return params;
}

}  // namespace bcos::evm
