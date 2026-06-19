#include "bcos-evm/opstack/L1BlockStorage.h"

#include "bcos-evm/opstack/OpStackConstants.h"
#include <algorithm>

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
}  // namespace

std::optional<IsthmusL1Attributes> parseIsthmusL1Attributes(bytesConstRef calldata)
{
    if (calldata.size() < ISTHMUS_L1_ATTRIBUTES_LEN)
    {
        return std::nullopt;
    }

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

evmc_bytes32 packL1FeeScalars(uint32_t baseFeeScalar, uint32_t blobBaseFeeScalar)
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
    return out;
}

evmc_bytes32 packOperatorFeeParams(uint32_t operatorFeeScalar, uint64_t operatorFeeConstant)
{
    evmc_bytes32 out{};
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
}  // namespace bcos::evm
