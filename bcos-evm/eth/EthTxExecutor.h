#pragma once

#include "bcos-evm/eth/EthTxFeeLedger.h"

namespace bcos::evm
{
using EthTxExecutor [[deprecated("use EthTxFeeLedger")]] = EthTxFeeLedger;
}  // namespace bcos::evm
