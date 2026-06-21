#pragma once

#include "bcos-codec/rlp/Common.h"
#include "bcos-codec/rlp/RLPDecode.h"
#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-framework/protocol/Transaction.h"
#include <algorithm>
#include <cstdint>
#include <string>

namespace bcos::evm::opstack_tx
{
namespace detail
{
inline bcos::bytesConstRef trimLeadingZeros(bcos::bytesConstRef input)
{
    if (input.empty())
    {
        return input;
    }
    auto const* it = std::find_if(input.begin(), input.end(), [](bcos::byte b) { return b != 0; });
    if (it == input.end())
    {
        return {};
    }
    return {it, static_cast<size_t>(input.end() - it)};
}

inline uint64_t parseChainIdField(std::string_view field)
{
    if (field.empty())
    {
        return 0;
    }
    try
    {
        return std::stoull(std::string(field));
    }
    catch (...)
    {
        return 0;
    }
}

inline size_t skipRlpItem(bcos::bytesRef& in)
{
    if (in.empty())
    {
        return 0;
    }
    if (in[0] < bcos::codec::rlp::BYTES_HEAD_BASE)
    {
        in = in.getCroppedData(1);
        return 1;
    }
    auto const before = in.size();
    auto [error, header] = bcos::codec::rlp::decodeHeader(in);
    if (error != nullptr)
    {
        return 0;
    }
    auto const headerBytes = before - in.size();
    if (in.size() < header.payloadLength)
    {
        return 0;
    }
    in = in.getCroppedData(header.payloadLength);
    return headerBytes + header.payloadLength;
}

inline size_t countListItems(bcos::bytesConstRef listPayload)
{
    bcos::bytes scratch(listPayload.begin(), listPayload.end());
    bcos::bytesRef remaining = bcos::ref(scratch);
    size_t count = 0;
    while (!remaining.empty())
    {
        if (skipRlpItem(remaining) == 0)
        {
            break;
        }
        ++count;
    }
    return count;
}

inline bcos::bytes stripTrailingListItems(bcos::bytesConstRef listPayload, size_t dropCount)
{
    bcos::bytes scratch(listPayload.begin(), listPayload.end());
    bcos::bytesRef remaining = bcos::ref(scratch);
    std::vector<size_t> boundaries{0};
    size_t consumed = 0;
    while (!remaining.empty())
    {
        auto const itemLen = skipRlpItem(remaining);
        if (itemLen == 0)
        {
            return {};
        }
        consumed += itemLen;
        boundaries.push_back(consumed);
    }
    if (boundaries.size() <= dropCount + 1)
    {
        return {};
    }
    auto const keepBytes = boundaries[boundaries.size() - dropCount - 1];
    return {listPayload.data(), listPayload.data() + keepBytes};
}

inline bcos::bytes encodeSignatureFields(uint64_t v, bcos::bytesConstRef r, bcos::bytesConstRef s)
{
    bcos::bytes encoded;
    bcos::codec::rlp::encode(encoded, v);
    bcos::codec::rlp::encode(encoded, trimLeadingZeros(r));
    bcos::codec::rlp::encode(encoded, trimLeadingZeros(s));
    return encoded;
}

inline bcos::bytes wrapSignedList(
    bcos::bytes const& unsignedListBody, bcos::bytes const& signatureEncoded, uint8_t typeByte)
{
    bcos::bytes signedList;
    bcos::codec::rlp::encodeHeader(signedList,
        {.isList = true, .payloadLength = unsignedListBody.size() + signatureEncoded.size()});
    signedList.insert(signedList.end(), unsignedListBody.begin(), unsignedListBody.end());
    signedList.insert(signedList.end(), signatureEncoded.begin(), signatureEncoded.end());

    bcos::bytes out;
    if (typeByte != 0)
    {
        out.push_back(typeByte);
    }
    out.insert(out.end(), signedList.begin(), signedList.end());
    return out;
}
}  // namespace detail

/// Reconstruct Ethereum signed tx bytes (MarshalBinary) from encodeForSign payload + FISCO
/// signature.
inline bcos::bytes encodeWeb3SignedMarshalBinary(protocol::Transaction const& tx)
{
    auto const unsignedPayload = tx.extraTransactionBytes();
    auto const signatureData = tx.signatureData();
    if (unsignedPayload.empty() || signatureData.size() < bcos::crypto::SECP256K1_SIGNATURE_LEN)
    {
        return {};
    }

    bcos::bytesConstRef r(signatureData.data(), 32);
    bcos::bytesConstRef s(signatureData.data() + 32, 32);
    auto const recoveryId = static_cast<uint8_t>(signatureData[64]);

    uint8_t typeByte = 0;
    bcos::bytes rlpBuffer(unsignedPayload.begin(), unsignedPayload.end());
    if (unsignedPayload[0] > 0 && unsignedPayload[0] < bcos::codec::rlp::BYTES_HEAD_BASE)
    {
        typeByte = unsignedPayload[0];
        rlpBuffer.erase(rlpBuffer.begin());
    }

    bcos::bytesRef rlpMutable = bcos::ref(rlpBuffer);
    auto [headerError, listHeader] = bcos::codec::rlp::decodeHeader(rlpMutable);
    if (headerError != nullptr || !listHeader.isList ||
        rlpMutable.size() < listHeader.payloadLength)
    {
        return {};
    }

    bcos::bytesConstRef const listPayload{rlpMutable.data(), listHeader.payloadLength};
    bcos::bytes unsignedListBody;
    bcos::bytes signatureEncoded;

    if (typeByte != 0)
    {
        unsignedListBody.assign(listPayload.begin(), listPayload.end());
        signatureEncoded = detail::encodeSignatureFields(recoveryId, r, s);
    }
    else
    {
        auto const itemCount = detail::countListItems(listPayload);
        auto const chainId = detail::parseChainIdField(tx.chainId());
        if (itemCount >= 9 || chainId != 0)
        {
            unsignedListBody = detail::stripTrailingListItems(listPayload, 3);
            if (unsignedListBody.empty())
            {
                return {};
            }
            auto const v = chainId * 2 + 35 + recoveryId;
            signatureEncoded = detail::encodeSignatureFields(v, r, s);
        }
        else
        {
            unsignedListBody.assign(listPayload.begin(), listPayload.end());
            signatureEncoded =
                detail::encodeSignatureFields(static_cast<uint64_t>(recoveryId) + 27, r, s);
        }
    }

    return detail::wrapSignedList(unsignedListBody, signatureEncoded, typeByte);
}

}  // namespace bcos::evm::opstack_tx
