#include "bcos-evm/specs-tests/Eip7702StrictTxValidator.h"

#include "bcos-codec/rlp/RLPDecode.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <evmc/evmc.h>
#include <limits>

namespace bcos::evm::reference_tests
{
namespace
{

using bcos::codec::rlp::decodeHeader;

bool consumeRlpItem(bcos::bytesRef& payload)
{
    auto [error, header] = decodeHeader(payload);
    if (error != nullptr)
    {
        return false;
    }
    if (payload.size() < header.payloadLength)
    {
        return false;
    }
    payload = payload.getCroppedData(header.payloadLength);
    return true;
}

bool decodeStrictUint64(bcos::bytesRef& payload, uint64_t& value)
{
    auto [error, header] = decodeHeader(payload);
    if (error != nullptr)
    {
        return false;
    }
    if (header.isList)
    {
        return false;
    }
    if (payload.size() < header.payloadLength)
    {
        return false;
    }
    auto item = payload.getCroppedData(0, header.payloadLength);
    payload = payload.getCroppedData(header.payloadLength);
    if (item.empty())
    {
        value = 0;
        return true;
    }
    if (item.size() == 1 && item[0] <= 0x7f)
    {
        value = item[0];
        return true;
    }
    if (item[0] == 0)
    {
        return false;
    }
    if (item.size() > 8)
    {
        return false;
    }
    bcos::u256 quantity = 0;
    for (auto byte : item)
    {
        quantity = (quantity << 8) + byte;
    }
    if (quantity > std::numeric_limits<uint64_t>::max())
    {
        return false;
    }
    value = static_cast<uint64_t>(quantity);
    return true;
}

bool decodeStrictScalarBytes(bcos::bytesRef& payload, bcos::bytes& out, size_t maxLen)
{
    auto [error, header] = decodeHeader(payload);
    if (error != nullptr || header.isList)
    {
        return false;
    }
    if (payload.size() < header.payloadLength)
    {
        return false;
    }
    auto item = payload.getCroppedData(0, header.payloadLength);
    payload = payload.getCroppedData(header.payloadLength);
    if (item.empty())
    {
        out.clear();
        return true;
    }
    if (item.size() == 1 && item[0] <= 0x7f)
    {
        out.assign(item.begin(), item.end());
        return true;
    }
    if (item[0] == 0)
    {
        return false;
    }
    if (item.size() > maxLen)
    {
        return false;
    }
    out = item.toBytes();
    return true;
}

bool decodeStrictAddress(bcos::bytesRef& payload, bcos::bytes& out)
{
    if (!decodeStrictScalarBytes(payload, out, sizeof(evmc_address)))
    {
        return false;
    }
    return out.size() == sizeof(evmc_address);
}

bool validateAuthorizationList(bcos::bytesRef& payload)
{
    auto [error, header] = decodeHeader(payload);
    if (error != nullptr || !header.isList)
    {
        return false;
    }
    if (payload.size() < header.payloadLength)
    {
        return false;
    }
    auto authItems = payload.getCroppedData(0, header.payloadLength);
    payload = payload.getCroppedData(header.payloadLength);
    if (authItems.empty())
    {
        return false;
    }

    while (!authItems.empty())
    {
        auto [entryError, entryHeader] = decodeHeader(authItems);
        if (entryError != nullptr || !entryHeader.isList)
        {
            return false;
        }
        if (authItems.size() < entryHeader.payloadLength)
        {
            return false;
        }
        auto entry = authItems.getCroppedData(0, entryHeader.payloadLength);
        authItems = authItems.getCroppedData(entryHeader.payloadLength);

        bcos::bytes chainIdRaw;
        bcos::bytes addressRaw;
        uint64_t nonce = 0;
        uint64_t yParity = 0;
        bcos::bytes r;
        bcos::bytes s;
        if (!decodeStrictScalarBytes(entry, chainIdRaw, 32) ||
            !decodeStrictAddress(entry, addressRaw) || !decodeStrictUint64(entry, nonce) ||
            !decodeStrictUint64(entry, yParity) || !decodeStrictScalarBytes(entry, r, 32) ||
            !decodeStrictScalarBytes(entry, s, 32))
        {
            return false;
        }
        if (!entry.empty() || r.size() > 32 || s.size() > 32 || yParity > 1)
        {
            return false;
        }
    }
    return true;
}

}  // namespace

bool validateStrictEip7702TypedTx(bcos::bytesConstRef txbytes)
{
    if (txbytes.empty() || static_cast<uint8_t>(txbytes[0]) != 0x04)
    {
        return false;
    }

    bcos::bytes copy(txbytes.begin(), txbytes.end());
    bcos::bytesRef payload(copy.data() + 1, copy.size() - 1);
    if (payload.empty())
    {
        return false;
    }

    auto [txError, txHeader] = decodeHeader(payload);
    if (txError != nullptr || !txHeader.isList)
    {
        return false;
    }
    if (payload.size() < txHeader.payloadLength)
    {
        return false;
    }
    auto txItems = payload.getCroppedData(0, txHeader.payloadLength);
    payload = payload.getCroppedData(txHeader.payloadLength);
    if (!payload.empty())
    {
        return false;
    }

    if (!consumeRlpItem(txItems))
    {
        return false;
    }
    uint64_t nonce = 0;
    if (!decodeStrictUint64(txItems, nonce))
    {
        return false;
    }
    for (size_t i = 0; i < 7; ++i)
    {
        if (!consumeRlpItem(txItems))
        {
            return false;
        }
    }
    if (!validateAuthorizationList(txItems))
    {
        return false;
    }
    for (size_t i = 0; i < 3; ++i)
    {
        if (!consumeRlpItem(txItems))
        {
            return false;
        }
    }
    return txItems.empty();
}

}  // namespace bcos::evm::reference_tests
