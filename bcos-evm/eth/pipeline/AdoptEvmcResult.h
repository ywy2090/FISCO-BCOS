#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include <evmc/evmc.hpp>

namespace bcos::crypto
{
class Hash;
}

namespace bcos::evm
{
inline EVMCResult adoptEvmcResult(evmc::Result&& result, bcos::crypto::Hash const& hashImpl)
{
    auto raw = result.release_raw();
    auto [status, ignored] = evmcStatusToErrorMessage(hashImpl, raw.status_code);
    (void)ignored;
    return EVMCResult(raw, status);
}
}  // namespace bcos::evm
