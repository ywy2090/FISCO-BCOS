#include "bcos-evm/opstack/adapter/OpStackChainCallTargetAdapter.h"
#include "bcos-evm/eth/core/CallTargetTypes.h"
#include "bcos-evm/opstack/l1/GasPriceOraclePredeploy.h"
#include "bcos-evm/opstack/l1/L1BlockPredeploy.h"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include <cstring>

namespace bcos::evm
{
namespace
{
bool sameAddress(evmc_address const& left, evmc_address const& right) noexcept
{
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

struct StaticChainTarget
{
    evmc_address address;
    execution::WarmPolicy warmPolicy;
};

constexpr StaticChainTarget kOpStackStaticTargets[] = {
    {OP_L1_BLOCK_PREDEPLOY, execution::WarmPolicy::TxEntryIfStatic},
    {OP_GAS_PRICE_ORACLE_PREDEPLOY, execution::WarmPolicy::TxEntryIfStatic},
};

std::optional<StaticChainTarget const*> findStaticTarget(
    evmc_address const& executionAddress) noexcept
{
    for (auto const& entry : kOpStackStaticTargets)
    {
        if (sameAddress(executionAddress, entry.address))
        {
            return &entry;
        }
    }
    return std::nullopt;
}
}  // namespace

OpStackChainCallTargetAdapter::OpStackChainCallTargetAdapter(state::State* state,
    bcos::u256 l2BaseFee, OpStackForkSchedule forkSchedule, uint64_t blockTimestamp)
  : m_state(state),
    m_l2BaseFee(std::move(l2BaseFee)),
    m_forkSchedule(std::move(forkSchedule)),
    m_blockTimestamp(blockTimestamp)
{}

std::optional<execution::CallTargetDescriptor> OpStackChainCallTargetAdapter::classifyTarget(
    state::State& /*state*/, evmc_address const& executionAddress, evmc_message const& /*msg*/,
    execution::FrameScope /*scope*/)
{
    auto const* entry = findStaticTarget(executionAddress).value_or(nullptr);
    if (entry == nullptr)
    {
        return std::nullopt;
    }

    return execution::CallTargetDescriptor{
        .kind = execution::CallTargetKind::ChainPrecompile,
        .dispatchAddress = entry->address,
        .warmPolicy = entry->warmPolicy,
    };
}

std::optional<evmc_result> OpStackChainCallTargetAdapter::dispatch(
    evmc_revision /*rev*/, evmc_message const& msg)
{
    if (m_state == nullptr)
    {
        return std::nullopt;
    }

    auto const target = msg.code_address;
    if (sameAddress(target, OP_L1_BLOCK_PREDEPLOY))
    {
        return L1BlockPredeploy::dispatch(*m_state, msg);
    }
    if (sameAddress(target, OP_GAS_PRICE_ORACLE_PREDEPLOY))
    {
        return GasPriceOraclePredeploy::dispatch(
            *m_state, msg, m_l2BaseFee, m_forkSchedule, m_blockTimestamp);
    }
    return std::nullopt;
}

void OpStackChainCallTargetAdapter::forEachStaticWarmTarget(
    std::function<void(evmc_address const&)> const& consume) const
{
    for (auto const& entry : kOpStackStaticTargets)
    {
        if (execution::isTxEntryWarm(entry.warmPolicy))
        {
            consume(entry.address);
        }
    }
}

}  // namespace bcos::evm
