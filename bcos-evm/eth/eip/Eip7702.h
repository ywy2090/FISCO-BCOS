#pragma once

#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <optional>
#include <vector>

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm
{
/// EIP-7702: charged per authorization during apply when authority already exists.
constexpr uint64_t PER_AUTH_BASE_COST = 12'500;
/// EIP-7702: worst-case intrinsic gas per authorization tuple (PER_EMPTY_ACCOUNT_COST).
constexpr uint64_t PER_EMPTY_ACCOUNT_COST = 25'000;

struct SetCodeAuthorization
{
    std::optional<bcos::u256> chainId;
    evmc_address authority{};
    evmc_address address{};
    uint64_t nonce{0};
    /// When present, authority is recovered from the signature at apply time.
    std::optional<uint64_t> yParity;
    bcos::bytes signatureR;
    bcos::bytes signatureS;
};

bool validateAuthorizationSignatureValues(
    uint64_t yParity, bcos::bytesConstRef r, bcos::bytesConstRef s) noexcept;
std::optional<evmc_address> recoverAuthorizationAuthority(bcos::u256 const& chainId,
    evmc_address const& address, uint64_t nonce, uint64_t yParity, bcos::bytesConstRef r,
    bcos::bytesConstRef s) noexcept;

std::optional<evmc_address> parseDelegationTarget(bcos::bytesConstRef code);
bcos::bytes addressToDelegation(evmc_address const& target);

void applyAuthorizations(state::State& state,
    std::vector<SetCodeAuthorization> const& authorizations, bcos::u256 const& txChainId);
void warmDelegationTarget(state::State& state, evmc_address const& recipient);
}  // namespace bcos::evm
