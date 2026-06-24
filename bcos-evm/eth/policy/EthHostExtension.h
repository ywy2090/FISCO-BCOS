#pragma once

#include "bcos-evm/eth/policy/EthVmHostPolicy.h"

namespace bcos::evm::state
{
using EthHostExtension [[deprecated("use EthVmHostPolicy")]] = EthVmHostPolicy;
}  // namespace bcos::evm::state
