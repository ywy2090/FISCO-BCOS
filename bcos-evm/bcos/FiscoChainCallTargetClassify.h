#pragma once

#include <evmc/evmc.h>
#include <optional>
#include <string_view>

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm
{

bool isFiscoPrecompileAddress(evmc_address const& address) noexcept;

std::optional<evmc_address> parseDynamicPrecompileTarget(std::string_view code) noexcept;

/// Resolve FISCO chain precompile dispatch address from executionAddress.
std::optional<evmc_address> resolveFiscoChainDispatchAddress(
    state::State& state, evmc_address const& executionAddress);

/// Legacy tryChainPrecompile routing: msg target + dynamic marker → routed message.
std::optional<evmc_message> routeFiscoChainPrecompileMessage(state::State* state, evmc_message msg);

}  // namespace bcos::evm
