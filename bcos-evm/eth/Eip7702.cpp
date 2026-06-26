#include "bcos-evm/eth/Eip7702.h"
#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include <array>

namespace bcos::evm
{
namespace
{
constexpr uint8_t DELEGATION_PREFIX_0 = 0xEF;
constexpr uint8_t DELEGATION_PREFIX_1 = 0x01;
constexpr uint8_t DELEGATION_PREFIX_2 = 0x00;
constexpr size_t DELEGATION_CODE_SIZE = 23;

// geth crypto/secp256k1 curve order N and lower-half bound (homestead=true).
bcos::u256 const SECP256K1_N{"0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141"};
bcos::u256 const SECP256K1_HALF_N{
    "0x7fffffffffffffffffffffffffffffff5d576e7357a4501ddfe92f46681b20a0"};

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

bcos::u256 bytesToU256(bcos::bytesConstRef bytes)
{
    bcos::bytes normalized(32, 0);
    if (bytes.size() > 32)
    {
        return bcos::u256(-1);
    }
    std::copy(bytes.begin(), bytes.end(), normalized.begin() + (32 - bytes.size()));
    return bcos::fromBigEndian<bcos::u256>(
        bcos::bytesConstRef{normalized.data(), normalized.size()});
}

bool hasSignatureMaterial(SetCodeAuthorization const& authorization) noexcept
{
    return authorization.yParity.has_value() &&
           (!authorization.signatureR.empty() || !authorization.signatureS.empty());
}

std::optional<evmc_address> resolveAuthority(SetCodeAuthorization const& authorization)
{
    if (!hasSignatureMaterial(authorization))
    {
        return std::nullopt;
    }

    return recoverAuthorizationAuthority(authorization.chainId.value_or(0), authorization.address,
        authorization.nonce, *authorization.yParity,
        bcos::bytesConstRef{authorization.signatureR.data(), authorization.signatureR.size()},
        bcos::bytesConstRef{authorization.signatureS.data(), authorization.signatureS.size()});
}
}  // namespace

bool validateAuthorizationSignatureValues(
    uint64_t yParity, bcos::bytesConstRef r, bcos::bytesConstRef s) noexcept
{
    if (yParity > 1 || r.size() > 32 || s.size() > 32 || r.empty() || s.empty())
    {
        return false;
    }

    auto const rValue = bytesToU256(r);
    auto const sValue = bytesToU256(s);
    if (rValue == 0 || sValue == 0 || rValue >= SECP256K1_N || sValue > SECP256K1_HALF_N)
    {
        return false;
    }
    return true;
}

std::optional<evmc_address> recoverAuthorizationAuthority(bcos::u256 const& chainId,
    evmc_address const& address, uint64_t nonce, uint64_t yParity, bcos::bytesConstRef r,
    bcos::bytesConstRef s) noexcept
{
    if (!validateAuthorizationSignatureValues(yParity, r, s))
    {
        return std::nullopt;
    }

    bcos::bytes signature(65, 0);
    std::copy(r.begin(), r.end(), signature.begin() + (32 - r.size()));
    std::copy(s.begin(), s.end(), signature.begin() + 32 + (32 - s.size()));
    signature[64] = static_cast<bcos::byte>(yParity);

    bcos::bytes encodedPayload;
    bcos::FixedBytes<20> targetFixed{bcos::bytesConstRef{address.bytes, sizeof(address.bytes)}};
    bcos::codec::rlp::encode(encodedPayload, chainId, targetFixed, nonce);

    bcos::bytes signPayload;
    signPayload.reserve(1 + encodedPayload.size());
    signPayload.push_back(0x05);
    signPayload.insert(signPayload.end(), encodedPayload.begin(), encodedPayload.end());

    bcos::crypto::Keccak256 hashImpl;
    auto const hash = bcos::crypto::keccak256Hash(bcos::ref(signPayload));
    bcos::crypto::Secp256k1Crypto signatureImpl;
    try
    {
        auto const [recoverOk, recovered] = signatureImpl.recoverAddress(
            hashImpl, hash, bcos::bytesConstRef{signature.data(), signature.size()});
        if (!recoverOk || recovered.size() != sizeof(evmc_address))
        {
            return std::nullopt;
        }

        evmc_address authority{};
        std::copy(recovered.begin(), recovered.end(), authority.bytes);
        return authority;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

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

void applyAuthorizations(state::State& state,
    std::vector<SetCodeAuthorization> const& authorizations, bcos::u256 const& txChainId)
{
    for (auto const& authorization : authorizations)
    {
        if (authorization.chainId.has_value() && *authorization.chainId != 0 &&
            *authorization.chainId != txChainId)
        {
            continue;
        }

        // EIP-2681: reject nonce increment overflow before signature recovery (geth ordering).
        if (authorization.nonce + 1 < authorization.nonce)
        {
            continue;
        }

        auto const authorityOpt = resolveAuthority(authorization);
        if (!authorityOpt.has_value())
        {
            continue;
        }
        auto const authority = *authorityOpt;

        (void)state.warm_up_address(authority);

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

        if (state.get_account(authority).has_value())
        {
            state.add_refund(PER_EMPTY_ACCOUNT_COST - PER_AUTH_BASE_COST);
        }

        state.set_nonce(authority, authorization.nonce + 1);
        if (isZeroAddress(authorization.address))
        {
            state.set_code(authority, {}, state::emptyCodeHash());
        }
        else
        {
            auto const delegation = addressToDelegation(authorization.address);
            state.set_code(authority, delegation,
                state::keccak256Code(bcos::bytesConstRef{delegation.data(), delegation.size()}));
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
