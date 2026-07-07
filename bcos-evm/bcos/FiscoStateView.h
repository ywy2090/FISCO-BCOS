#pragma once

#include "bcos-evm/storage/LedgerStateView.h"

namespace bcos::evm::state
{
using FiscoStateView [[deprecated("use LedgerStateView from bcos-evm/storage/LedgerStateView.h")]] =
    LedgerStateView;
}  // namespace bcos::evm::state
