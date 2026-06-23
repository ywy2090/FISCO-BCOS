#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/state/State.hpp"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <cstdint>
#include <optional>

namespace bcos::evm
{
struct OpStackPreDebitEntryInput
{
    evmc_message const& message;
    state::State& state;
    uint64_t gasLimit;
    bool skipTransactionChecks;
    bcos::bytesConstRef inputData;
    uint64_t& floorDataGasOut;
};

std::optional<EVMCResult> opStackPreDebitEntry(OpStackPreDebitEntryInput const& input);
}  // namespace bcos::evm
