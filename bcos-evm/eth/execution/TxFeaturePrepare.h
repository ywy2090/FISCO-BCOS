#pragma once

#include "bcos-evm/eth/execution/CreateContract.h"
#include "bcos-evm/eth/state/Transaction.hpp"
#include <evmc/evmc.h>

namespace bcos::evm::execution
{
inline void setWarmDestinationFromKind(state::TransactionProperties& props, evmc_call_kind kind)
{
    props.warmDestination = !isCreateKind(kind);
}
}  // namespace bcos::evm::execution
