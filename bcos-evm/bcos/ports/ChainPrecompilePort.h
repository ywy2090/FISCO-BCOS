#pragma once

#include <evmc/evmc.h>
#include <optional>

namespace bcos::evm
{

struct ChainPrecompilePort
{
    virtual ~ChainPrecompilePort() = default;
    virtual std::optional<evmc_result> dispatch(evmc_revision rev, evmc_message const& msg) = 0;
};

}  // namespace bcos::evm
