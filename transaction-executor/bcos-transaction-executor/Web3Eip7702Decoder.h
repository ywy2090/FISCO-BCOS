#pragma once

#include "bcos-codec/rlp/RLPDecode.h"
#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-evm/eth/Eip7702.h"
#include <algorithm>
#include <optional>
#include <vector>

namespace bcos::evm::web3_tx
{
inline bool skipRlpItem(bcos::bytesRef& payload)
{
    auto [error, header] = bcos::codec::rlp::decodeHeader(payload);
    if (error != nullptr)
    {
        return false;
    }
    payload = payload.getCroppedData(header.payloadLength);
    return true;
}

inline bool decodeU256Bytes(bcos::bytes const& raw, bcos::u256& value)
{
    value = bcos::fromBigEndian<bcos::u256>(bcos::bytesConstRef{raw.data(), raw.size()});
    return true;
}

inline std::optional<evmc_address> recoverAuthorizationAuthority(bcos::u256 const& chainId,
    evmc_address const& target, uint64_t nonce, uint64_t yParity, bcos::bytes const& rRaw,
    bcos::bytes const& sRaw)
{
    if (yParity > 1)
    {
        return std::nullopt;
    }

    bcos::bytes signature(65, 0);
    if (rRaw.size() > 32 || sRaw.size() > 32)
    {
        return std::nullopt;
    }
    std::copy(rRaw.begin(), rRaw.end(), signature.begin() + (32 - rRaw.size()));
    std::copy(sRaw.begin(), sRaw.end(), signature.begin() + 32 + (32 - sRaw.size()));
    signature[64] = static_cast<bcos::byte>(yParity);

    bcos::bytes encodedPayload;
    bcos::FixedBytes<20> targetFixed{bcos::bytesConstRef{target.bytes, sizeof(target.bytes)}};
    bcos::codec::rlp::encode(encodedPayload, chainId, targetFixed, nonce);

    bcos::bytes signPayload;
    signPayload.reserve(1 + encodedPayload.size());
    signPayload.push_back(0x05);
    signPayload.insert(signPayload.end(), encodedPayload.begin(), encodedPayload.end());

    bcos::crypto::Keccak256 hashImpl;
    auto hash = bcos::crypto::keccak256Hash(bcos::ref(signPayload));
    bcos::crypto::Secp256k1Crypto signatureImpl;
    auto [recoverOk, recovered] = signatureImpl.recoverAddress(
        hashImpl, hash, bcos::bytesConstRef{signature.data(), signature.size()});
    if (!recoverOk || recovered.size() != sizeof(evmc_address))
    {
        return std::nullopt;
    }

    evmc_address authority{};
    std::copy(recovered.begin(), recovered.end(), authority.bytes);
    return authority;
}

/// Decode EIP-7702 (type 0x04) authorization list from typed-tx envelope bytes.
inline std::optional<std::vector<SetCodeAuthorization>> decodeEip7702Authorizations(
    bcos::bytesConstRef extra)
{
    if (extra.empty() || static_cast<uint8_t>(extra[0]) != 0x04)
    {
        return std::nullopt;
    }

    bcos::bytes copy(extra.begin(), extra.end());
    bcos::bytesRef payload(copy.data() + 1, copy.size() - 1);
    auto [txError, txHeader] = bcos::codec::rlp::decodeHeader(payload);
    if (txError != nullptr || !txHeader.isList)
    {
        return std::nullopt;
    }
    auto txItems = payload.getCroppedData(0, txHeader.payloadLength);
    payload = payload.getCroppedData(txHeader.payloadLength);
    (void)payload;

    // Skip: chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data,
    // accessList
    for (size_t i = 0; i < 9; ++i)
    {
        if (!skipRlpItem(txItems))
        {
            return std::nullopt;
        }
    }

    auto [authError, authHeader] = bcos::codec::rlp::decodeHeader(txItems);
    if (authError != nullptr || !authHeader.isList)
    {
        return std::nullopt;
    }
    auto authItems = txItems.getCroppedData(0, authHeader.payloadLength);
    txItems = txItems.getCroppedData(authHeader.payloadLength);
    (void)txItems;

    std::vector<SetCodeAuthorization> out;
    while (!authItems.empty())
    {
        auto [entryError, entryHeader] = bcos::codec::rlp::decodeHeader(authItems);
        if (entryError != nullptr || !entryHeader.isList)
        {
            return std::nullopt;
        }
        auto entry = authItems.getCroppedData(0, entryHeader.payloadLength);
        authItems = authItems.getCroppedData(entryHeader.payloadLength);

        bcos::bytes chainIdRaw;
        bcos::bytes addressRaw;
        uint64_t nonce = 0;
        uint64_t yParity = 0;
        bcos::bytes r;
        bcos::bytes s;
        if (auto error =
                bcos::codec::rlp::decodeItems(entry, chainIdRaw, addressRaw, nonce, yParity, r, s);
            error != nullptr)
        {
            return std::nullopt;
        }
        if (!entry.empty() || addressRaw.size() != sizeof(evmc_address))
        {
            return std::nullopt;
        }

        SetCodeAuthorization authorization;
        bcos::u256 chainId = 0;
        decodeU256Bytes(chainIdRaw, chainId);
        authorization.chainId = chainId;
        std::copy(addressRaw.begin(), addressRaw.end(), authorization.address.bytes);
        authorization.nonce = nonce;

        auto authority =
            recoverAuthorizationAuthority(chainId, authorization.address, nonce, yParity, r, s);
        if (!authority.has_value())
        {
            continue;
        }
        authorization.authority = *authority;
        out.emplace_back(std::move(authorization));
    }

    return out;
}

struct Eip4844BlobFields
{
    bcos::u256 maxFeePerBlobGas;
    std::vector<bcos::h256> blobVersionedHashes;
};

/// Decode EIP-4844 (type 0x03) blob fee cap and versioned hashes from typed-tx envelope bytes.
inline std::optional<Eip4844BlobFields> decodeEip4844BlobFields(bcos::bytesConstRef extra)
{
    if (extra.empty() || static_cast<uint8_t>(extra[0]) != 0x03)
    {
        return std::nullopt;
    }

    bcos::bytes copy(extra.begin(), extra.end());
    bcos::bytesRef payload(copy.data() + 1, copy.size() - 1);
    auto [txError, txHeader] = bcos::codec::rlp::decodeHeader(payload);
    if (txError != nullptr || !txHeader.isList)
    {
        return std::nullopt;
    }
    auto txItems = payload.getCroppedData(0, txHeader.payloadLength);

    // Skip: chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data,
    // accessList
    for (size_t i = 0; i < 9; ++i)
    {
        if (!skipRlpItem(txItems))
        {
            return std::nullopt;
        }
    }

    Eip4844BlobFields fields;
    if (auto error = bcos::codec::rlp::decodeItems(
            txItems, fields.maxFeePerBlobGas, fields.blobVersionedHashes);
        error != nullptr)
    {
        return std::nullopt;
    }
    return fields;
}
}  // namespace bcos::evm::web3_tx
