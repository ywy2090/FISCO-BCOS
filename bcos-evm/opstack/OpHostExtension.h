#pragma once

#include "bcos-evm/opstack/OpStackVmHostPolicy.h"

namespace bcos::evm
{
using OpHostExtension [[deprecated("use OpStackVmHostPolicy")]] = OpStackVmHostPolicy;
}  // namespace bcos::evm
