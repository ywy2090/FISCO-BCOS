#pragma once

#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-framework/executor/OpStackTxType.h"

namespace bcos::evm
{

inline bool isDepositTx(OpStackExecutionRequest const& input) noexcept
{
    return input.web3TypedTxKind == bcos::executor::DEPOSIT_TX_TYPE || input.depositTx.has_value();
}

}  // namespace bcos::evm
