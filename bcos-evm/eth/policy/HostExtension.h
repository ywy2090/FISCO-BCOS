#pragma once

#include "bcos-evm/eth/policy/VmHostPolicy.h"

namespace bcos::evm::state
{
using HostExtension [[deprecated("use VmHostPolicy")]] = VmHostPolicy;
}  // namespace bcos::evm::state
