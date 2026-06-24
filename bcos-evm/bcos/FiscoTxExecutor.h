#pragma once

#include "bcos-evm/bcos/FiscoTxFeeLedger.h"

namespace bcos::evm
{
using FiscoTxExecutor [[deprecated("use FiscoTxFeeLedger")]] = FiscoTxFeeLedger;
}  // namespace bcos::evm
