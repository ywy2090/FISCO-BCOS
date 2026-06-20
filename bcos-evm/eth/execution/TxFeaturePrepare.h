#pragma once

#include "bcos-evm/eth/state/Transaction.hpp"
#include <evmc/evmc.h>

namespace bcos::evm::execution
{
inline bool isCreateKind(evmc_call_kind kind) noexcept
{
    return kind == EVMC_CREATE || kind == EVMC_CREATE2;
}

inline void setWarmDestinationFromKind(state::TransactionProperties& props, evmc_call_kind kind)
{
    props.warmDestination = !isCreateKind(kind);
}
}  // namespace bcos::evm::execution
