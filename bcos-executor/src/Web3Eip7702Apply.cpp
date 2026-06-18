#include "Web3Eip7702Apply.h"
#include "bcos-codec/rlp/RLPEncode.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/signature/Exceptions.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <cstring>
#include <intx/intx.hpp>
#include <limits>
#include <span>

namespace bcos::executor
{
namespace
{
constexpr uint8_t EIP7702_SIGN_MAGIC = 0x05;
constexpr auto SECP256K1_N = intx::from_string<intx::uint256>(
    "0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141");
constexpr auto SECP256K1_HALF_N = SECP256K1_N / 2;

intx::uint256 loadH256AsUint256(bcos::h256 const& hash) noexcept
{
    return intx::be::load<intx::uint256>(
        std::span<uint8_t const, bcos::h256::SIZE>(hash.data(), bcos::h256::SIZE));
}

bool validateEip7702SignatureValues(Eip7702Authorization const& auth) noexcept
{
    if (auth.yParity != 0 && auth.yParity != 1)
    {
        return false;
    }
    auto const r = loadH256AsUint256(auth.r);
    auto const s = loadH256AsUint256(auth.s);
    if (r < 1 || s < 1)
    {
        return false;
    }
    if (r >= SECP256K1_N || s >= SECP256K1_N)
    {
        return false;
    }
    if (s > SECP256K1_HALF_N)
    {
        return false;
    }
    return true;
}

uint64_t evmcChainIdToU64(evmc_uint256be const& chainId) noexcept
{
    auto const v = intx::be::load<intx::uint256>(chainId);
    if (v > std::numeric_limits<uint64_t>::max())
    {
        return std::numeric_limits<uint64_t>::max();
    }
    return static_cast<uint64_t>(v);
}
}  // namespace

bool isEip7702DelegationIndicator(bcos::bytesConstRef code) noexcept
{
    if (code.size() != EIP7702_DELEGATION_CODE_SIZE)
    {
        return false;
    }
    return std::memcmp(code.data(), EIP7702_DELEGATION_PREFIX, sizeof(EIP7702_DELEGATION_PREFIX)) ==
           0;
}

std::optional<bcos::Address> recoverEip7702Authority(
    crypto::Hash::Ptr const& hashImpl, Eip7702Authorization const& auth)
{
    if (!validateEip7702SignatureValues(auth))
    {
        return std::nullopt;
    }
    bcos::bytes rlpList;
    codec::rlp::encode(rlpList, auth.chainId, auth.address, auth.nonce);

    bcos::bytes signDomain;
    signDomain.reserve(1 + rlpList.size());
    signDomain.push_back(EIP7702_SIGN_MAGIC);
    signDomain.insert(signDomain.end(), rlpList.begin(), rlpList.end());

    auto const hash = hashImpl->hash(bcos::bytesConstRef(
        reinterpret_cast<bcos::byte const*>(signDomain.data()), signDomain.size()));

    bcos::bytes signature(crypto::SECP256K1_SIGNATURE_LEN, 0);
    std::memcpy(signature.data(), auth.r.data(), sizeof(auth.r));
    std::memcpy(
        signature.data() + crypto::SECP256K1_SIGNATURE_R_LEN, auth.s.data(), sizeof(auth.s));
    signature[crypto::SECP256K1_SIGNATURE_V] = auth.yParity;

    static crypto::Secp256k1Crypto const secp;
    try
    {
        auto pub = secp.recover(
            hash, bcos::bytesConstRef(
                      reinterpret_cast<bcos::byte const*>(signature.data()), signature.size()));
        if (!pub)
        {
            return std::nullopt;
        }
        return crypto::calculateAddress(hashImpl, pub);
    }
    catch (bcos::crypto::InvalidSignature const&)
    {
        return std::nullopt;
    }
}

std::vector<bcos::Address> collectRecoveredEip7702Authorities(
    crypto::Hash::Ptr const& hashImpl, protocol::Transaction const& tx)
{
    std::vector<bcos::Address> authorities;
    auto const parsed = parseEip7702FromWeb3Transaction(tx);
    if (!parsed.authorizationList || parsed.authorizationList->empty())
    {
        return authorities;
    }
    authorities.reserve(parsed.authorizationList->size());
    for (auto const& tuple : *parsed.authorizationList)
    {
        if (auto const authority = recoverEip7702Authority(hashImpl, tuple))
        {
            authorities.push_back(*authority);
        }
    }
    return authorities;
}

evmc_address addressToEvmc(bcos::Address const& addr) noexcept
{
    evmc_address out{};
    static_assert(sizeof(out.bytes) == bcos::Address::SIZE);
    std::memcpy(out.bytes, addr.data(), bcos::Address::SIZE);
    return out;
}

bool eip7702ChainIdMatches(
    uint64_t tupleChainId, std::optional<evmc_uint256be> const& ledgerChainId) noexcept
{
    if (tupleChainId == 0)
    {
        return true;
    }
    if (!ledgerChainId)
    {
        return false;
    }
    return tupleChainId == evmcChainIdToU64(*ledgerChainId);
}

}  // namespace bcos::executor
