#pragma once

#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-framework/protocol/BlockHeader.h"
#include <bcos-utilities/Common.h>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace bcos::evm
{
inline constexpr uint32_t OP_HEADER_FEE_MAGIC = 0x4F504631;  // "OPF1" big-endian
inline constexpr size_t OP_HEADER_COINBASE_SIZE = sizeof(evmc_address);
inline constexpr size_t OP_HEADER_FEE_EXTENSION_SIZE = 64;
inline constexpr bcos::u256 OP_MIN_BLOB_GAS_PRICE{1};

struct OpStackHeaderFees
{
    bcos::u256 baseFee;
    uint64_t excessBlobGas{0};
};

namespace opstack_header_detail
{
inline uint32_t readU32BE(bytesConstRef data, size_t offset)
{
    return (static_cast<uint32_t>(data[offset]) << 24) |
           (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) | static_cast<uint32_t>(data[offset + 3]);
}

inline uint64_t readU64BE(bytesConstRef data, size_t offset)
{
    uint64_t value = 0;
    for (size_t i = 0; i < sizeof(uint64_t); ++i)
    {
        value = (value << 8) | static_cast<uint64_t>(data[offset + i]);
    }
    return value;
}

inline void writeU32BE(bcos::bytes& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

inline void writeU64BE(bcos::bytes& out, uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xff));
    }
}

inline void writeU256BE(bcos::bytes& out, bcos::u256 const& value)
{
    auto encoded = toBigEndian(value);
    if (encoded.size() < 32)
    {
        out.insert(out.end(), 32 - encoded.size(), 0);
    }
    out.insert(out.end(), encoded.begin(), encoded.end());
}
}  // namespace opstack_header_detail

inline bcos::u256 calcOpStackBlobBaseFee(uint64_t excessBlobGas)
{
    if (excessBlobGas != 0)
    {
        throw std::invalid_argument(
            "OpStack Isthmus requires excessBlobGas=0 for execution blobBaseFee");
    }
    return OP_MIN_BLOB_GAS_PRICE;
}

inline std::optional<OpStackHeaderFees> tryParseOpStackHeaderFees(
    bcos::protocol::BlockHeader const& header)
{
    auto const extra = header.extraData();
    if (extra.size() < OP_HEADER_FEE_EXTENSION_SIZE)
    {
        return std::nullopt;
    }
    if (opstack_header_detail::readU32BE(extra, OP_HEADER_COINBASE_SIZE) != OP_HEADER_FEE_MAGIC)
    {
        return std::nullopt;
    }

    OpStackHeaderFees fees;
    evmc_bytes32 baseFeeBytes{};
    std::memcpy(
        baseFeeBytes.bytes, extra.data() + OP_HEADER_COINBASE_SIZE + 4, sizeof(baseFeeBytes.bytes));
    fees.baseFee = state::fromEvmC(baseFeeBytes);
    fees.excessBlobGas = opstack_header_detail::readU64BE(extra, OP_HEADER_COINBASE_SIZE + 4 + 32);
    return fees;
}

inline OpStackHeaderFees requireOpStackHeaderFees(bcos::protocol::BlockHeader const& header)
{
    if (auto parsed = tryParseOpStackHeaderFees(header))
    {
        return *parsed;
    }
    throw std::invalid_argument("OpStack block header missing OPF1 fee extension in extraData");
}

inline bcos::bytes encodeOpStackHeaderExtra(
    evmc_address coinbase, bcos::u256 baseFee, uint64_t excessBlobGas = 0)
{
    bcos::bytes extra;
    extra.reserve(OP_HEADER_FEE_EXTENSION_SIZE);
    extra.insert(extra.end(), coinbase.bytes, coinbase.bytes + sizeof(coinbase.bytes));
    opstack_header_detail::writeU32BE(extra, OP_HEADER_FEE_MAGIC);
    opstack_header_detail::writeU256BE(extra, baseFee);
    opstack_header_detail::writeU64BE(extra, excessBlobGas);
    return extra;
}

inline bcos::u256 resolveOpStackBaseFeeFromHeader(bcos::protocol::BlockHeader const& blockHeader)
{
    return requireOpStackHeaderFees(blockHeader).baseFee;
}

inline bcos::u256 resolveOpStackBlobBaseFeeFromHeader(
    bcos::protocol::BlockHeader const& blockHeader)
{
    auto const fees = requireOpStackHeaderFees(blockHeader);
    return calcOpStackBlobBaseFee(fees.excessBlobGas);
}
}  // namespace bcos::evm
