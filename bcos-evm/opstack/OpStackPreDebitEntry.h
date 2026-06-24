#pragma once

#include "bcos-evm/opstack/OpStackFloorGasPrecheck.h"

namespace bcos::evm
{
using OpStackFloorGasPrecheckInput [[deprecated("use OpStackFloorGasPrecheckInput")]] =
    OpStackFloorGasPrecheckInput;

[[deprecated("use opStackFloorGasPrecheck")]] inline std::optional<EVMCResult> opStackFloorGasPrecheck(
    OpStackFloorGasPrecheckInput const& input)
{
    return opStackFloorGasPrecheck(input);
}
}  // namespace bcos::evm
