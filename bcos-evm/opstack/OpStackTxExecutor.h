#pragma once

#include "bcos-evm/opstack/OpStackTxFeeLedger.h"

namespace bcos::evm
{
using OpStackTxExecutor [[deprecated("use OpStackTxFeeLedger")]] = OpStackTxFeeLedger;
}  // namespace bcos::evm
