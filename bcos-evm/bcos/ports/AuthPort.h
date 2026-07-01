#pragma once

#include "bcos-evm/eth/kernel/EVMCResult.h"
#include <evmc/evmc.h>
#include <optional>
#include <string_view>

namespace bcos::evm
{

struct AuthPort
{
    virtual ~AuthPort() = default;
    virtual std::optional<EVMCResult> checkAuth(evmc_message const& msg) = 0;
    virtual void createAuthTable(evmc_message const& msg, std::string_view tablePath) = 0;
};

}  // namespace bcos::evm
