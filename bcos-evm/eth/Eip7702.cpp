#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include <array>

namespace bcos::evm
{
namespace
{
constexpr uint64_t TX_AUTH_TUPLE_GAS = 12'500;
constexpr uint64_t CALL_NEW_ACCOUNT_GAS = 25'000;
constexpr uint8_t DELEGATION_PREFIX_0 = 0xEF;
constexpr uint8_t DELEGATION_PREFIX_1 = 0x01;
constexpr uint8_t DELEGATION_PREFIX_2 = 0x00;
constexpr size_t DELEGATION_CODE_SIZE = 23;

bool isZeroAddress(evmc_address const& address) noexcept
{
    for (auto byte : address.bytes)
    {
        if (byte != 0)
        {
            return false;
        }
    }
    return true;
}
}  // namespace

std::optional<evmc_address> parseDelegationTarget(bcos::bytesConstRef code)
{
    if (code.size() != DELEGATION_CODE_SIZE || code[0] != DELEGATION_PREFIX_0 ||
        code[1] != DELEGATION_PREFIX_1 || code[2] != DELEGATION_PREFIX_2)
    {
        return std::nullopt;
    }

    evmc_address target{};
    std::copy(code.begin() + 3, code.end(), target.bytes);
    return target;
}

bcos::bytes addressToDelegation(evmc_address const& target)
{
    bcos::bytes code;
    code.reserve(DELEGATION_CODE_SIZE);
    code.push_back(DELEGATION_PREFIX_0);
    code.push_back(DELEGATION_PREFIX_1);
    code.push_back(DELEGATION_PREFIX_2);
    code.insert(code.end(), target.bytes, target.bytes + sizeof(target.bytes));
    return code;
}

void applyAuthorizations(state::State& state, std::vector<SetCodeAuthorization> const& authorizations,
    bcos::u256 const& txChainId)
{
    for (auto const& authorization : authorizations)
    {
        auto const authority = authorization.authority;
        if (state::isZeroAddress(authority))
        {
            continue;
        }
        if (authorization.chainId.has_value() && *authorization.chainId != 0 &&
            *authorization.chainId != txChainId)
        {
            continue;
        }
        if (state.get_nonce(authority) != authorization.nonce)
        {
            continue;
        }
        auto const currentCode = state.get_code(authority);
        if (!currentCode.empty() &&
            !parseDelegationTarget(bcos::bytesConstRef{currentCode.data(), currentCode.size()})
                 .has_value())
        {
            continue;
        }

        (void)state.warm_up_address(authority);
        if (state.get_account(authority).has_value())
        {
            state.add_refund(CALL_NEW_ACCOUNT_GAS - TX_AUTH_TUPLE_GAS);
        }

        state.set_nonce(authority, authorization.nonce + 1);
        if (isZeroAddress(authorization.address))
        {
            state.set_code(authority, {}, {});
        }
        else
        {
            state.set_code(authority, addressToDelegation(authorization.address), {});
        }
    }
}

void warmDelegationTarget(state::State& state, evmc_address const& recipient)
{
    auto const code = state.get_code(recipient);
    auto const target = parseDelegationTarget(bcos::bytesConstRef{code.data(), code.size()});
    if (target.has_value())
    {
        (void)state.warm_up_address(*target);
    }
}
}  // namespace bcos::evm
