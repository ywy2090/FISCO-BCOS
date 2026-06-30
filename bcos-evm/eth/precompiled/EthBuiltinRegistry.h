#pragma once

/*
 * @brief Legacy registry API: executor/pricer lookup by address suffix.
 *
 * Populated by EthBuiltinRegistry.cpp (macro-registered impls). Consumed by
 * PrecompiledContract and transaction-executor adapters; the kernel hot path
 * uses EthPrecompiles + kPrecompileTable instead.
 */

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
