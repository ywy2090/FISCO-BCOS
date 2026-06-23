#pragma once

#include "InMemoryAuthAdapter.h"
#include "InMemoryChainPrecompileAdapter.h"

namespace bcos::evm::test
{
using TestAuthPort = InMemoryAuthAdapter;
using TestChainPrecompilePort = InMemoryChainPrecompileAdapter;
}  // namespace bcos::evm::test
