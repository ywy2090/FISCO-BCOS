#include "bcos-evm/bcos/FiscoChainCallTargetClassify.h"
#include "bcos-evm/bcos/FiscoConstants.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "eth/state/StateKeyHash.hpp"
#include <stdint.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace bcos::evm
{
namespace
{
constexpr std::string_view PRECOMPILED_CODE_FIELD = "[PRECOMPILED]";

bool startsWith(std::string_view input, std::string_view prefix) noexcept
{
    return input.size() >= prefix.size() &&
           std::memcmp(input.data(), prefix.data(), prefix.size()) == 0;
}

std::vector<std::string_view> splitByComma(std::string_view input)
{
    std::vector<std::string_view> tokens;
    size_t begin = 0;
    while (begin <= input.size())
    {
        auto const end = input.find(',', begin);
        if (end == std::string_view::npos)
        {
            tokens.push_back(input.substr(begin));
            break;
        }
        tokens.push_back(input.substr(begin, end - begin));
        begin = end + 1;
    }
    return tokens;
}
}  // namespace

bool isFiscoPrecompileAddress(evmc_address const& address) noexcept
{
    for (size_t i = 0; i < 12; ++i)
    {
        if (address.bytes[i] != 0)
        {
            return false;
        }
    }

    uint64_t value = 0;
    for (size_t i = 12; i < sizeof(address.bytes); ++i)
    {
        value = (value << 8U) | static_cast<uint64_t>(address.bytes[i]);
    }
    return value >= FISCO_PRECOMPILE_MIN;
}

std::optional<evmc_address> parseDynamicPrecompileTarget(std::string_view code) noexcept
{
    if (!startsWith(code, PRECOMPILED_CODE_FIELD))
    {
        return std::nullopt;
    }

    auto const tokens = splitByComma(code);
    if (tokens.size() < 2)
    {
        return std::nullopt;
    }

    auto target = state::parseHexAddress(tokens[1]);
    if (state::isZeroAddress(target))
    {
        return std::nullopt;
    }
    return target;
}

std::optional<evmc_address> resolveFiscoChainDispatchAddress(
    state::State& state, evmc_address const& executionAddress)
{
    auto resolvedTarget = executionAddress;
    auto const code = state.get_code(executionAddress);
    if (!code.empty())
    {
        std::string_view codeView(reinterpret_cast<char const*>(code.data()), code.size());
        if (auto dynamicTarget = parseDynamicPrecompileTarget(codeView))
        {
            resolvedTarget = *dynamicTarget;
        }
    }

    if (!isFiscoPrecompileAddress(resolvedTarget))
    {
        return std::nullopt;
    }
    return resolvedTarget;
}

std::optional<evmc_message> routeFiscoChainPrecompileMessage(state::State* state, evmc_message msg)
{
    evmc_address const zero{};
    auto const target = std::memcmp(msg.code_address.bytes, zero.bytes, sizeof(zero.bytes)) != 0 ?
                            msg.code_address :
                            msg.recipient;
    auto resolvedTarget = target;
    if (state != nullptr)
    {
        auto const code = state->get_code(target);
        if (!code.empty())
        {
            std::string_view codeView(reinterpret_cast<char const*>(code.data()), code.size());
            if (auto dynamicTarget = parseDynamicPrecompileTarget(codeView))
            {
                resolvedTarget = *dynamicTarget;
            }
        }
    }

    if (!isFiscoPrecompileAddress(resolvedTarget))
    {
        return std::nullopt;
    }

    msg.recipient = resolvedTarget;
    msg.code_address = resolvedTarget;
    return msg;
}

}  // namespace bcos::evm
