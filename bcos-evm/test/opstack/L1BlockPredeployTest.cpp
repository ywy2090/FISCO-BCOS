#define BOOST_TEST_MODULE L1BlockPredeployTest

#include "bcos-evm/opstack/l1/L1BlockPredeploy.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/l1/L1BlockSelectors.h"
#include "helpers/InMemoryEvmStateReader.h"
#include <boost/test/included/unit_test.hpp>
#include <algorithm>
#include <fstream>
#include <string>

namespace bcos::evm::test
{
namespace
{
bytes loadFixture(std::string_view name)
{
    auto const path = std::string(OPSTACK_FIXTURES_DIR) + "/" + std::string(name);
    std::ifstream input(path, std::ios::binary);
    BOOST_REQUIRE_MESSAGE(input.is_open(), "missing fixture: " << path);
    return {std::istreambuf_iterator<char>(input), {}};
}

evmc_message makeCall(bytes const& input, evmc_address sender)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = OP_L1_BLOCK_PREDEPLOY;
    message.code_address = OP_L1_BLOCK_PREDEPLOY;
    message.input_data = input.data();
    message.input_size = input.size();
    return message;
}

bytes selectorInput(uint32_t selector)
{
    return {static_cast<uint8_t>((selector >> 24) & 0xff),
        static_cast<uint8_t>((selector >> 16) & 0xff), static_cast<uint8_t>((selector >> 8) & 0xff),
        static_cast<uint8_t>(selector & 0xff)};
}

bytes hexToBytes(std::string_view hex)
{
    bytes out;
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        auto const byte = std::stoi(std::string(hex.substr(i, 2)), nullptr, 16);
        out.push_back(static_cast<uint8_t>(byte));
    }
    return out;
}

bytes callGetterOutput(state::State& state, uint32_t selector)
{
    auto payload = selectorInput(selector);
    auto result = L1BlockPredeploy::dispatch(state, makeCall(payload, OP_DEPOSITOR_ACCOUNT));
    BOOST_REQUIRE(result.has_value());
    BOOST_REQUIRE_EQUAL(result->status_code, EVMC_SUCCESS);
    bytes out(result->output_data, result->output_data + result->output_size);
    if (result->release != nullptr)
    {
        result->release(&result.value());
    }
    return out;
}

void checkGetterHex(state::State& state, uint32_t selector, std::string_view expectedHex)
{
    auto const out = callGetterOutput(state, selector);
    auto const expected = hexToBytes(expectedHex);
    BOOST_CHECK_EQUAL_COLLECTIONS(out.begin(), out.end(), expected.begin(), expected.end());
}
}  // namespace

BOOST_AUTO_TEST_CASE(setter_unpacks_isthmus_fixture_into_slots)
{
    state::test::InMemoryEvmStateReader baseState;
    state::State state(baseState);
    auto const calldata = loadFixture("isthmus_l1_attributes.bin");

    auto result = L1BlockPredeploy::dispatch(state, makeCall(calldata, OP_DEPOSITOR_ACCOUNT));
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_EQUAL(result->status_code, EVMC_SUCCESS);

    auto const numberTs =
        state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_NUMBER_TIMESTAMP_SLOT));
    for (size_t i = 0; i < 8; ++i)
    {
        BOOST_CHECK_EQUAL(numberTs.bytes[16 + i], static_cast<uint8_t>(0x11 + i));
        BOOST_CHECK_EQUAL(numberTs.bytes[24 + i], static_cast<uint8_t>(0x21 + i));
    }

    auto const baseFee = state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BASE_FEE_SLOT));
    auto const hash = state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_HASH_SLOT));
    auto const blobBaseFee =
        state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BLOB_BASE_FEE_SLOT));
    auto const feeScalars =
        state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_FEE_SCALARS_SLOT));
    auto const batcherHash =
        state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BATCHER_HASH_SLOT));
    auto const operatorFee =
        state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(OPERATOR_FEE_PARAMS_SLOT));

    BOOST_CHECK_EQUAL(state::fromEvmC(baseFee), u256(0x0123456789abcdefULL));
    BOOST_CHECK_EQUAL(state::fromEvmC(blobBaseFee), u256(0x0fedcba987654321ULL));

    BOOST_CHECK_EQUAL(feeScalars.bytes[16], 0x11);
    BOOST_CHECK_EQUAL(feeScalars.bytes[17], 0x22);
    BOOST_CHECK_EQUAL(feeScalars.bytes[18], 0x33);
    BOOST_CHECK_EQUAL(feeScalars.bytes[19], 0x44);
    BOOST_CHECK_EQUAL(feeScalars.bytes[20], 0x55);
    BOOST_CHECK_EQUAL(feeScalars.bytes[21], 0x66);
    BOOST_CHECK_EQUAL(feeScalars.bytes[22], 0x77);
    BOOST_CHECK_EQUAL(feeScalars.bytes[23], 0x88);
    for (size_t i = 0; i < 8; ++i)
    {
        BOOST_CHECK_EQUAL(feeScalars.bytes[24 + i], static_cast<uint8_t>(0x01 + i));
    }

    auto const expectedHash =
        hexToBytes("0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20");
    for (size_t i = 0; i < 32; ++i)
    {
        BOOST_CHECK_EQUAL(hash.bytes[i], expectedHash[i]);
    }

    auto const expectedBatcher =
        hexToBytes("2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40");
    for (size_t i = 0; i < 32; ++i)
    {
        BOOST_CHECK_EQUAL(batcherHash.bytes[i], expectedBatcher[i]);
    }

    BOOST_CHECK_EQUAL(operatorFee.bytes[18], 0x00);
    BOOST_CHECK_EQUAL(operatorFee.bytes[19], 0x00);
    BOOST_CHECK_EQUAL(operatorFee.bytes[20], 0xa1);
    BOOST_CHECK_EQUAL(operatorFee.bytes[21], 0xb2);
    BOOST_CHECK_EQUAL(operatorFee.bytes[22], 0xc3);
    BOOST_CHECK_EQUAL(operatorFee.bytes[23], 0xd4);
    BOOST_CHECK_EQUAL(operatorFee.bytes[24], 0x01);
    BOOST_CHECK_EQUAL(operatorFee.bytes[25], 0x02);
    BOOST_CHECK_EQUAL(operatorFee.bytes[26], 0x03);
    BOOST_CHECK_EQUAL(operatorFee.bytes[27], 0x04);
    BOOST_CHECK_EQUAL(operatorFee.bytes[28], 0x05);
    BOOST_CHECK_EQUAL(operatorFee.bytes[29], 0x06);
    BOOST_CHECK_EQUAL(operatorFee.bytes[30], 0x07);
    BOOST_CHECK_EQUAL(operatorFee.bytes[31], 0x08);
}

BOOST_AUTO_TEST_CASE(setter_rejects_non_depositor_sender)
{
    state::test::InMemoryEvmStateReader baseState;
    state::State state(baseState);
    auto const calldata = loadFixture("isthmus_l1_attributes.bin");
    evmc_address sender{};
    sender.bytes[19] = 0x99;

    auto result = L1BlockPredeploy::dispatch(state, makeCall(calldata, sender));
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_EQUAL(result->status_code, EVMC_REVERT);
    BOOST_CHECK(state::isZeroBytes32(
        state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BASE_FEE_SLOT))));
}

BOOST_AUTO_TEST_CASE(getters_return_slot_values_after_setter)
{
    state::test::InMemoryEvmStateReader baseState;
    state::State state(baseState);
    auto const calldata = loadFixture("isthmus_l1_attributes.bin");

    auto writeResult = L1BlockPredeploy::dispatch(state, makeCall(calldata, OP_DEPOSITOR_ACCOUNT));
    BOOST_REQUIRE(writeResult.has_value());
    BOOST_REQUIRE_EQUAL(writeResult->status_code, EVMC_SUCCESS);

    checkGetterHex(state, l1block::kNumber,
        "0000000000000000000000000000000000000000000000002122232425262728");
    checkGetterHex(state, l1block::kTimestamp,
        "0000000000000000000000000000000000000000000000001112131415161718");
    checkGetterHex(state, l1block::kSequenceNumber,
        "0000000000000000000000000000000000000000000000000102030405060708");
    checkGetterHex(state, l1block::kBasefee,
        "0000000000000000000000000000000000000000000000000123456789abcdef");
    checkGetterHex(state, l1block::kL1BaseFee,
        "0000000000000000000000000000000000000000000000000123456789abcdef");
    checkGetterHex(state, l1block::kBlobBaseFee,
        "0000000000000000000000000000000000000000000000000fedcba987654321");
    checkGetterHex(state, l1block::kL1BlobBaseFee,
        "0000000000000000000000000000000000000000000000000fedcba987654321");
    checkGetterHex(state, l1block::kBaseFeeScalar,
        "0000000000000000000000000000000000000000000000000000000011223344");
    checkGetterHex(state, l1block::kBlobBaseFeeScalar,
        "0000000000000000000000000000000000000000000000000000000055667788");
    checkGetterHex(
        state, l1block::kHash, "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20");
    checkGetterHex(state, l1block::kBatcherHash,
        "2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40");
    checkGetterHex(state, l1block::kL1FeeOverhead,
        "0000000000000000000000000000000000000000000000000000000000000000");
    checkGetterHex(state, l1block::kL1FeeScalar,
        "0000000000000000000000000000000000000000000000000000000000000000");
    checkGetterHex(state, l1block::kOperatorFeeScalar,
        "00000000000000000000000000000000000000000000000000000000a1b2c3d4");
    checkGetterHex(state, l1block::kOperatorFeeConstant,
        "0000000000000000000000000000000000000000000000000102030405060708");
    checkGetterHex(state, l1block::kDaFootprintGasScalar,
        "0000000000000000000000000000000000000000000000000000000000000000");
}

BOOST_AUTO_TEST_CASE(pure_getters_match_l1block_constants)
{
    state::test::InMemoryEvmStateReader baseState;
    state::State state(baseState);

    checkGetterHex(state, l1block::kDepositorAccount,
        "000000000000000000000000deaddeaddeaddeaddeaddeaddeaddeaddead0001");
    checkGetterHex(state, l1block::kIsCustomGasToken,
        "0000000000000000000000000000000000000000000000000000000000000000");
    checkGetterHex(state, l1block::kGasPayingToken,
        "000000000000000000000000eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee00000000000000000000000000"
        "00000000000000000000000000000000000012");
    checkGetterHex(state, l1block::kGasPayingTokenName,
        "000000000000000000000000000000000000000000000000000000000000002000000000000000000000000000"
        "000000000000000000000000000000000000054574686572000000000000000000000000000000000000000000"
        "000000000000");
    checkGetterHex(state, l1block::kGasPayingTokenSymbol,
        "000000000000000000000000000000000000000000000000000000000000002000000000000000000000000000"
        "000000000000000000000000000000000000034554480000000000000000000000000000000000000000000000"
        "000000000000");
    checkGetterHex(state, l1block::kVersion,
        "000000000000000000000000000000000000000000000000000000000000002000000000000000000000000000"
        "00000000000000000000000000000000000005312e392e30000000000000000000000000000000000000000000"
        "000000000000");
}

BOOST_AUTO_TEST_CASE(isFeatureEnabled_returns_false_by_default)
{
    state::test::InMemoryEvmStateReader baseState;
    state::State state(baseState);

    bytes input = selectorInput(l1block::kIsFeatureEnabled);
    evmc_bytes32 key{};
    key.bytes[31] = 0x42;
    input.insert(input.end(), key.bytes, key.bytes + 32);

    auto result = L1BlockPredeploy::dispatch(state, makeCall(input, OP_DEPOSITOR_ACCOUNT));
    BOOST_REQUIRE(result.has_value());
    BOOST_REQUIRE_EQUAL(result->status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(result->output_size, size_t(32));
    evmc_bytes32 raw{};
    std::copy(result->output_data, result->output_data + 32, raw.bytes);
    if (result->release != nullptr)
    {
        result->release(&result.value());
    }
    BOOST_CHECK(state::isZeroBytes32(raw));
}

}  // namespace bcos::evm::test
