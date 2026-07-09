#pragma once

#include <evmc/evmc.h>

namespace bcos::evmref::opstack
{
enum class OpFork
{
    Ecotone,
    Fjord,
    Granite,
    Holocene,
    Isthmus,
};

struct PrecompileOverrides;

struct OpForkConfig
{
    OpFork fork;
    evmc_revision rev;
    const PrecompileOverrides* precompiles;
    bool disable_prague_requests;
    bool has_operator_fee;
};

const OpForkConfig& isthmusConfig() noexcept;
}  // namespace bcos::evmref::opstack
