#include "bcos-evm/opstack/l1/L1BlockStorage.h"

#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include <algorithm>
#include <cstring>
#include <evmone_precompiles/keccak.hpp>

namespace bcos::evm
{
namespace
{
uint32_t readU32(bytesConstRef calldata, size_t offset)
{
    return (static_cast<uint32_t>(calldata[offset]) << 24) |
           (static_cast<uint32_t>(calldata[offset + 1]) << 16) |
           (static_cast<uint32_t>(calldata[offset + 2]) << 8) |
           static_cast<uint32_t>(calldata[offset + 3]);
}

uint64_t readU64(bytesConstRef calldata, size_t offset)
{
    uint64_t value = 0;
    for (size_t i = 0; i < sizeof(uint64_t); ++i)
    {
        value = (value << 8) | static_cast<uint64_t>(calldata[offset + i]);
    }
    return value;
}

evmc_bytes32 readBytes32(bytesConstRef calldata, size_t offset)
{
    evmc_bytes32 out{};
    std::copy(calldata.data() + offset, calldata.data() + offset + 32, out.bytes);
    return out;
}

void writeU64BigEndian(uint8_t* dest, uint64_t value)
{
    for (size_t i = 0; i < 8; ++i)
    {
        dest[i] = static_cast<uint8_t>((value >> (56 - i * 8)) & 0xff);
    }
}
}  // namespace

std::optional<IsthmusL1Attributes> parseIsthmusL1Attributes(bytesConstRef calldata)
{
    if (calldata.size() < ISTHMUS_L1_ATTRIBUTES_LEN)
    {
        return std::nullopt;
    }

    // Calldata: selector(4) + ABI-encoded tuple (see op-geth deposit tx construction)
    IsthmusL1Attributes parsed;
    parsed.baseFeeScalar = readU32(calldata, 4);
    parsed.blobBaseFeeScalar = readU32(calldata, 8);
    parsed.sequenceNumber = readU64(calldata, 12);
    parsed.timestamp = readU64(calldata, 20);
    parsed.l1BlockNumber = readU64(calldata, 28);
    parsed.l1BaseFee = readBytes32(calldata, 36);
    parsed.l1BlobBaseFee = readBytes32(calldata, 68);
    parsed.hash = readBytes32(calldata, 100);
    parsed.batcherHash = readBytes32(calldata, 132);
    parsed.operatorFeeScalar = readU32(calldata, 164);
    parsed.operatorFeeConstant = readU64(calldata, 168);
    return parsed;
}

std::optional<JovianL1Attributes> parseJovianL1Attributes(bytesConstRef calldata)
{
    if (calldata.size() < JOVIAN_L1_ATTRIBUTES_LEN)
    {
        return std::nullopt;
    }

    auto const isthmus = parseIsthmusL1Attributes(calldata);
    if (!isthmus.has_value())
    {
        return std::nullopt;
    }

    JovianL1Attributes parsed;
    static_cast<IsthmusL1Attributes&>(parsed) = *isthmus;
    parsed.daFootprintGasScalar = static_cast<uint16_t>(
        (static_cast<uint16_t>(calldata[176]) << 8) | static_cast<uint16_t>(calldata[177]));
    return parsed;
}

evmc_bytes32 packL1NumberTimestamp(uint64_t timestamp, uint64_t number)
{
    evmc_bytes32 out{};
    writeU64BigEndian(out.bytes + 16, timestamp);
    writeU64BigEndian(out.bytes + 24, number);
    return out;
}

evmc_bytes32 packL1FeeScalarsSlot(
    uint32_t baseFeeScalar, uint32_t blobBaseFeeScalar, uint64_t sequenceNumber)
{
    evmc_bytes32 out{};
    constexpr size_t kOffset = 16;
    out.bytes[kOffset] = static_cast<uint8_t>((baseFeeScalar >> 24) & 0xff);
    out.bytes[kOffset + 1] = static_cast<uint8_t>((baseFeeScalar >> 16) & 0xff);
    out.bytes[kOffset + 2] = static_cast<uint8_t>((baseFeeScalar >> 8) & 0xff);
    out.bytes[kOffset + 3] = static_cast<uint8_t>(baseFeeScalar & 0xff);
    out.bytes[kOffset + 4] = static_cast<uint8_t>((blobBaseFeeScalar >> 24) & 0xff);
    out.bytes[kOffset + 5] = static_cast<uint8_t>((blobBaseFeeScalar >> 16) & 0xff);
    out.bytes[kOffset + 6] = static_cast<uint8_t>((blobBaseFeeScalar >> 8) & 0xff);
    out.bytes[kOffset + 7] = static_cast<uint8_t>(blobBaseFeeScalar & 0xff);
    writeU64BigEndian(out.bytes + 24, sequenceNumber);
    return out;
}

evmc_bytes32 packOperatorFeeParams(
    uint32_t operatorFeeScalar, uint64_t operatorFeeConstant, uint16_t daFootprintGasScalar)
{
    evmc_bytes32 out{};
    out.bytes[18] = static_cast<uint8_t>((daFootprintGasScalar >> 8) & 0xff);
    out.bytes[19] = static_cast<uint8_t>(daFootprintGasScalar & 0xff);
    out.bytes[20] = static_cast<uint8_t>((operatorFeeScalar >> 24) & 0xff);
    out.bytes[21] = static_cast<uint8_t>((operatorFeeScalar >> 16) & 0xff);
    out.bytes[22] = static_cast<uint8_t>((operatorFeeScalar >> 8) & 0xff);
    out.bytes[23] = static_cast<uint8_t>(operatorFeeScalar & 0xff);
    out.bytes[24] = static_cast<uint8_t>((operatorFeeConstant >> 56) & 0xff);
    out.bytes[25] = static_cast<uint8_t>((operatorFeeConstant >> 48) & 0xff);
    out.bytes[26] = static_cast<uint8_t>((operatorFeeConstant >> 40) & 0xff);
    out.bytes[27] = static_cast<uint8_t>((operatorFeeConstant >> 32) & 0xff);
    out.bytes[28] = static_cast<uint8_t>((operatorFeeConstant >> 24) & 0xff);
    out.bytes[29] = static_cast<uint8_t>((operatorFeeConstant >> 16) & 0xff);
    out.bytes[30] = static_cast<uint8_t>((operatorFeeConstant >> 8) & 0xff);
    out.bytes[31] = static_cast<uint8_t>(operatorFeeConstant & 0xff);
    return out;
}

u256 unpackNumber(evmc_bytes32 const& packed)
{
    u256 value = 0;
    for (size_t i = 24; i < 32; ++i)
    {
        value = (value << 8) | (u256)packed.bytes[i];
    }
    return value;
}

u256 unpackTimestamp(evmc_bytes32 const& packed)
{
    u256 value = 0;
    for (size_t i = 16; i < 24; ++i)
    {
        value = (value << 8) | (u256)packed.bytes[i];
    }
    return value;
}

u256 unpackSequenceNumber(evmc_bytes32 const& packed)
{
    return unpackNumber(packed);
}

u256 unpackBaseFeeScalar(evmc_bytes32 const& packed)
{
    return ((u256)packed.bytes[16] << 24) | ((u256)packed.bytes[17] << 16) |
           ((u256)packed.bytes[18] << 8) | (u256)packed.bytes[19];
}

u256 unpackBlobBaseFeeScalar(evmc_bytes32 const& packed)
{
    return ((u256)packed.bytes[20] << 24) | ((u256)packed.bytes[21] << 16) |
           ((u256)packed.bytes[22] << 8) | (u256)packed.bytes[23];
}

u256 unpackOperatorFeeScalar(evmc_bytes32 const& packed)
{
    return ((u256)packed.bytes[20] << 24) | ((u256)packed.bytes[21] << 16) |
           ((u256)packed.bytes[22] << 8) | (u256)packed.bytes[23];
}

u256 unpackOperatorFeeConstant(evmc_bytes32 const& packed)
{
    u256 value = 0;
    for (size_t i = 24; i < 32; ++i)
    {
        value = (value << 8) | (u256)packed.bytes[i];
    }
    return value;
}

u256 unpackDaFootprintGasScalar(evmc_bytes32 const& packed)
{
    return ((u256)packed.bytes[18] << 8) | (u256)packed.bytes[19];
}

bytes encodeAbiString(std::string_view value)
{
    bytes out(64, 0);
    out[31] = 32;
    auto const length = value.size();
    out[63] = static_cast<uint8_t>(length & 0xff);
    if (length >= 256)
    {
        out[62] = static_cast<uint8_t>((length >> 8) & 0xff);
        out[61] = static_cast<uint8_t>((length >> 16) & 0xff);
        out[60] = static_cast<uint8_t>((length >> 24) & 0xff);
    }
    bytes data(length);
    std::copy(value.begin(), value.end(), data.begin());
    auto const padding = (32 - (length % 32)) % 32;
    data.resize(length + padding, 0);
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

bytes encodeAbiAddress(evmc_address const& address)
{
    bytes out(32, 0);
    std::copy(address.bytes, address.bytes + sizeof(address.bytes), out.begin() + 12);
    return out;
}

bytes encodeGasPayingToken()
{
    // Non-custom-gas-token chains return 0xeeee…eeee sentinel + offset 18
    bytes out(64, 0);
    constexpr uint8_t ether[20] = {0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee,
        0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee};
    std::copy(std::begin(ether), std::end(ether), out.begin() + 12);
    out[63] = 18;
    return out;
}

bool readFeatureEnabled(state::State& state, evmc_bytes32 const& key)
{
    // Solidity: mapping(bytes32 => bool) at slot L1_FEATURE_ENABLED_MAPPING_SLOT
    uint8_t buf[64];
    std::memcpy(buf, key.bytes, 32);
    std::memset(buf + 32, 0, 31);
    buf[63] = 9;
    auto const hash = ethash::keccak256(buf, 64);
    evmc_bytes32 slot{};
    std::memcpy(slot.bytes, hash.bytes, 32);
    return !state::isZeroBytes32(state.get_storage(OP_L1_BLOCK_PREDEPLOY, slot));
}
}  // namespace bcos::evm
