#pragma once

#include "bcos-utilities/Common.h"
#include <evmc/evmc.h>
#include <cstdint>
#include <functional>
#include <utility>

namespace bcos::evm
{

using PrecompiledExecutor = std::function<std::pair<bool, bytes>(bytesConstRef)>;
using PrecompiledPricer = std::function<bigint(bytesConstRef)>;
using RevisionAwarePricer = std::function<bigint(bytesConstRef, evmc_revision)>;

PrecompiledExecutor const& builtinExecutorBySuffix(uint16_t suffix);
PrecompiledPricer const& builtinPricerBySuffix(uint16_t suffix);
bool hasBuiltinPricerBySuffix(uint16_t suffix) noexcept;

}  // namespace bcos::evm
