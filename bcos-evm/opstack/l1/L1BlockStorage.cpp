#include "bcos-evm/opstack/l1/L1BlockStorage.h"

#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/l1/L1BlockLayout.h"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include "bcos-utilities/Common.h"
#include "eth/state/StateKeyHash.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <evmone_precompiles/keccak.hpp>
#include <utility>

namespace bcos::evm
{
namespace
{
// Calldata helpers: setL1BlockValues* uses a fixed big-endian layout (not standard ABI tuple).

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
    std::copy(
        calldata.data() + offset, calldata.data() + offset + l1block::EVM_WORD_SIZE, out.bytes);
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

    // Offsets after 4-byte selector (176 bytes total = ISTHMUS_L1_ATTRIBUTES_LEN):
    //   [4..8)   baseFeeScalar      [8..12)  blobBaseFeeScalar
    //   [12..20) sequenceNumber     [20..28) timestamp
    //   [28..36) l1BlockNumber      [36..68) l1BaseFee (bytes32)
    //   [68..100) l1BlobBaseFee     [100..132) hash
    //   [132..164) batcherHash      [164..168) operatorFeeScalar
    //   [168..176) operatorFeeConstant
    IsthmusL1Attributes parsed;
    parsed.baseFeeScalar = readU32(calldata, l1block::ATTR_OFFSET_BASE_FEE_SCALAR);
    parsed.blobBaseFeeScalar = readU32(calldata, l1block::ATTR_OFFSET_BLOB_BASE_FEE_SCALAR);
    parsed.sequenceNumber = readU64(calldata, l1block::ATTR_OFFSET_SEQUENCE_NUMBER);
    parsed.timestamp = readU64(calldata, l1block::ATTR_OFFSET_TIMESTAMP);
    parsed.l1BlockNumber = readU64(calldata, l1block::ATTR_OFFSET_L1_BLOCK_NUMBER);
    parsed.l1BaseFee = readBytes32(calldata, l1block::ATTR_OFFSET_L1_BASE_FEE);
    parsed.l1BlobBaseFee = readBytes32(calldata, l1block::ATTR_OFFSET_L1_BLOB_BASE_FEE);
    parsed.hash = readBytes32(calldata, l1block::ATTR_OFFSET_HASH);
    parsed.batcherHash = readBytes32(calldata, l1block::ATTR_OFFSET_BATCHER_HASH);
    parsed.operatorFeeScalar = readU32(calldata, l1block::ATTR_OFFSET_OPERATOR_FEE_SCALAR);
    parsed.operatorFeeConstant = readU64(calldata, l1block::ATTR_OFFSET_OPERATOR_FEE_CONSTANT);
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
    // Jovian appends uint16 daFootprintGasScalar at bytes [176..178).
    parsed.daFootprintGasScalar = static_cast<uint16_t>(
        (static_cast<uint16_t>(calldata[l1block::ATTR_OFFSET_DA_FOOTPRINT_GAS_SCALAR]) << 8) |
        static_cast<uint16_t>(calldata[l1block::ATTR_OFFSET_DA_FOOTPRINT_GAS_SCALAR + 1]));
    return parsed;
}

evmc_bytes32 packL1NumberTimestamp(uint64_t timestamp, uint64_t number)
{
    evmc_bytes32 out{};
    writeU64BigEndian(out.bytes + l1block::SLOT0_TIMESTAMP_BYTE, timestamp);
    writeU64BigEndian(out.bytes + l1block::SLOT0_NUMBER_BYTE, number);
    return out;
}

evmc_bytes32 packL1FeeScalarsSlot(
    uint32_t baseFeeScalar, uint32_t blobBaseFeeScalar, uint64_t sequenceNumber)
{
    evmc_bytes32 out{};
    constexpr size_t kOffset = l1block::SLOT3_SCALARS_BYTE;
    out.bytes[kOffset] = static_cast<uint8_t>((baseFeeScalar >> 24) & 0xff);
    out.bytes[kOffset + 1] = static_cast<uint8_t>((baseFeeScalar >> 16) & 0xff);
    out.bytes[kOffset + 2] = static_cast<uint8_t>((baseFeeScalar >> 8) & 0xff);
    out.bytes[kOffset + 3] = static_cast<uint8_t>(baseFeeScalar & 0xff);
    out.bytes[kOffset + 4] = static_cast<uint8_t>((blobBaseFeeScalar >> 24) & 0xff);
    out.bytes[kOffset + 5] = static_cast<uint8_t>((blobBaseFeeScalar >> 16) & 0xff);
    out.bytes[kOffset + 6] = static_cast<uint8_t>((blobBaseFeeScalar >> 8) & 0xff);
    out.bytes[kOffset + 7] = static_cast<uint8_t>(blobBaseFeeScalar & 0xff);
    writeU64BigEndian(out.bytes + l1block::SLOT3_SEQUENCE_BYTE, sequenceNumber);
    return out;
}

evmc_bytes32 packOperatorFeeParams(
    uint32_t operatorFeeScalar, uint64_t operatorFeeConstant, uint16_t daFootprintGasScalar)
{
    evmc_bytes32 out{};
    out.bytes[l1block::SLOT8_DA_FOOTPRINT_BYTE] =
        static_cast<uint8_t>((daFootprintGasScalar >> 8) & 0xff);
    out.bytes[l1block::SLOT8_DA_FOOTPRINT_BYTE + 1] =
        static_cast<uint8_t>(daFootprintGasScalar & 0xff);
    out.bytes[l1block::SLOT8_OPERATOR_SCALAR_BYTE] =
        static_cast<uint8_t>((operatorFeeScalar >> 24) & 0xff);
    out.bytes[l1block::SLOT8_OPERATOR_SCALAR_BYTE + 1] =
        static_cast<uint8_t>((operatorFeeScalar >> 16) & 0xff);
    out.bytes[l1block::SLOT8_OPERATOR_SCALAR_BYTE + 2] =
        static_cast<uint8_t>((operatorFeeScalar >> 8) & 0xff);
    out.bytes[l1block::SLOT8_OPERATOR_SCALAR_BYTE + 3] =
        static_cast<uint8_t>(operatorFeeScalar & 0xff);
    out.bytes[l1block::SLOT8_OPERATOR_CONSTANT_BYTE] =
        static_cast<uint8_t>((operatorFeeConstant >> 56) & 0xff);
    out.bytes[l1block::SLOT8_OPERATOR_CONSTANT_BYTE + 1] =
        static_cast<uint8_t>((operatorFeeConstant >> 48) & 0xff);
    out.bytes[l1block::SLOT8_OPERATOR_CONSTANT_BYTE + 2] =
        static_cast<uint8_t>((operatorFeeConstant >> 40) & 0xff);
    out.bytes[l1block::SLOT8_OPERATOR_CONSTANT_BYTE + 3] =
        static_cast<uint8_t>((operatorFeeConstant >> 32) & 0xff);
    out.bytes[l1block::SLOT8_OPERATOR_CONSTANT_BYTE + 4] =
        static_cast<uint8_t>((operatorFeeConstant >> 24) & 0xff);
    out.bytes[l1block::SLOT8_OPERATOR_CONSTANT_BYTE + 5] =
        static_cast<uint8_t>((operatorFeeConstant >> 16) & 0xff);
    out.bytes[l1block::SLOT8_OPERATOR_CONSTANT_BYTE + 6] =
        static_cast<uint8_t>((operatorFeeConstant >> 8) & 0xff);
    out.bytes[l1block::SLOT8_OPERATOR_CONSTANT_BYTE + 7] =
        static_cast<uint8_t>(operatorFeeConstant & 0xff);
    return out;
}

u256 unpackNumber(evmc_bytes32 const& packed)
{
    u256 value = 0;
    for (size_t i = l1block::SLOT0_NUMBER_BYTE; i < l1block::EVM_WORD_SIZE; ++i)
    {
        value = (value << 8) | (u256)packed.bytes[i];
    }
    return value;
}

u256 unpackTimestamp(evmc_bytes32 const& packed)
{
    u256 value = 0;
    for (size_t i = l1block::SLOT0_TIMESTAMP_BYTE; i < l1block::SLOT0_NUMBER_BYTE; ++i)
    {
        value = (value << 8) | (u256)packed.bytes[i];
    }
    return value;
}

u256 unpackSequenceNumber(evmc_bytes32 const& packed)
{
    // Slot 3 packs sequenceNumber in bytes[24..32), same layout as number in slot 0.
    return unpackNumber(packed);
}

u256 unpackBaseFeeScalar(evmc_bytes32 const& packed)
{
    return ((u256)packed.bytes[l1block::SLOT3_SCALARS_BYTE] << 24) |
           ((u256)packed.bytes[l1block::SLOT3_SCALARS_BYTE + 1] << 16) |
           ((u256)packed.bytes[l1block::SLOT3_SCALARS_BYTE + 2] << 8) |
           (u256)packed.bytes[l1block::SLOT3_SCALARS_BYTE + 3];
}

u256 unpackBlobBaseFeeScalar(evmc_bytes32 const& packed)
{
    return ((u256)packed.bytes[l1block::SLOT3_SCALARS_BYTE + 4] << 24) |
           ((u256)packed.bytes[l1block::SLOT3_SCALARS_BYTE + 5] << 16) |
           ((u256)packed.bytes[l1block::SLOT3_SCALARS_BYTE + 6] << 8) |
           (u256)packed.bytes[l1block::SLOT3_SCALARS_BYTE + 7];
}

u256 unpackOperatorFeeScalar(evmc_bytes32 const& packed)
{
    return ((u256)packed.bytes[l1block::SLOT8_OPERATOR_SCALAR_BYTE] << 24) |
           ((u256)packed.bytes[l1block::SLOT8_OPERATOR_SCALAR_BYTE + 1] << 16) |
           ((u256)packed.bytes[l1block::SLOT8_OPERATOR_SCALAR_BYTE + 2] << 8) |
           (u256)packed.bytes[l1block::SLOT8_OPERATOR_SCALAR_BYTE + 3];
}

u256 unpackOperatorFeeConstant(evmc_bytes32 const& packed)
{
    u256 value = 0;
    for (size_t i = l1block::SLOT0_NUMBER_BYTE; i < l1block::EVM_WORD_SIZE; ++i)
    {
        value = (value << 8) | (u256)packed.bytes[i];
    }
    return value;
}

u256 unpackDaFootprintGasScalar(evmc_bytes32 const& packed)
{
    return ((u256)packed.bytes[l1block::SLOT8_DA_FOOTPRINT_BYTE] << 8) |
           (u256)packed.bytes[l1block::SLOT8_DA_FOOTPRINT_BYTE + 1];
}

bytes encodeAbiString(std::string_view value)
{
    // ABI string: offset word (32) + length word + data padded to 32 bytes.
    bytes out(l1block::EVM_WORD_SIZE * 2, 0);
    out[l1block::EVM_WORD_SIZE - 1] = l1block::EVM_WORD_SIZE;
    auto const length = value.size();
    out[l1block::EVM_WORD_SIZE * 2 - 1] = static_cast<uint8_t>(length & 0xff);
    if (length >= 256)
    {
        out[l1block::EVM_WORD_SIZE * 2 - 2] = static_cast<uint8_t>((length >> 8) & 0xff);
        out[l1block::EVM_WORD_SIZE * 2 - 3] = static_cast<uint8_t>((length >> 16) & 0xff);
        out[l1block::EVM_WORD_SIZE * 2 - 4] = static_cast<uint8_t>((length >> 24) & 0xff);
    }
    bytes data(length);
    std::copy(value.begin(), value.end(), data.begin());
    auto const padding =
        (l1block::EVM_WORD_SIZE - (length % l1block::EVM_WORD_SIZE)) % l1block::EVM_WORD_SIZE;
    data.resize(length + padding, 0);
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

bytes encodeAbiAddress(evmc_address const& address)
{
    bytes out(l1block::EVM_WORD_SIZE, 0);
    std::copy(address.bytes, address.bytes + sizeof(address.bytes),
        out.begin() + (l1block::EVM_WORD_SIZE - l1block::EVM_ADDRESS_SIZE));
    return out;
}

bytes encodeGasPayingToken()
{
    // Non-custom-gas-token chains return 0xeeee…eeee sentinel + offset 18
    bytes out(l1block::EVM_WORD_SIZE * 2, 0);
    std::copy(l1block::ETH_GAS_PAYING_TOKEN_SENTINEL.begin(),
        l1block::ETH_GAS_PAYING_TOKEN_SENTINEL.end(),
        out.begin() + (l1block::EVM_WORD_SIZE - l1block::EVM_ADDRESS_SIZE));
    out[l1block::EVM_WORD_SIZE * 2 - 1] =
        static_cast<uint8_t>(l1block::GAS_PAYING_TOKEN_NAME_OFFSET);
    return out;
}

bool readFeatureEnabled(state::State& state, evmc_bytes32 const& key)
{
    // Solidity: mapping(bytes32 => bool) at slot L1_FEATURE_ENABLED_MAPPING_SLOT (9).
    // Storage key = keccak256(abi.encode(key, slot)).
    uint8_t buf[l1block::EVM_WORD_SIZE * 2];
    std::memcpy(buf, key.bytes, l1block::EVM_WORD_SIZE);
    std::memset(buf + l1block::EVM_WORD_SIZE, 0, l1block::EVM_WORD_SIZE - 1);
    buf[l1block::EVM_WORD_SIZE * 2 - 1] =
        static_cast<uint8_t>(static_cast<uint64_t>(L1_FEATURE_ENABLED_MAPPING_SLOT));
    auto const hash = ethash::keccak256(buf, l1block::EVM_WORD_SIZE * 2);
    evmc_bytes32 slot{};
    std::memcpy(slot.bytes, hash.bytes, 32);
    return !state::isZeroBytes32(state.get_storage(OP_L1_BLOCK_PREDEPLOY, slot));
}
}  // namespace bcos::evm
