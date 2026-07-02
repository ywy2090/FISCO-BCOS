#pragma once

#include "bcos-evm/eth/kernel/EVMCResult.h"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <cstdint>
#include <optional>

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm
{
struct OpStackFloorGasPrecheckInput
{
    evmc_message const& message;
    state::State& state;
    uint64_t gasLimit;
    bool skipTransactionChecks;
    bcos::bytesConstRef inputData;
    uint64_t& floorDataGasOut;
};

std::optional<EVMCResult> opStackFloorGasPrecheck(OpStackFloorGasPrecheckInput const& input);
}  // namespace bcos::evm
