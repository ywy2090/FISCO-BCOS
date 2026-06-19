#define BOOST_TEST_MODULE L1BlockPredeployTest

#include "bcos-evm/opstack/L1BlockPredeploy.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "state/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <algorithm>
#include <fstream>

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

u256 callGetter(state::State& state, uint32_t selector)
{
    auto payload = selectorInput(selector);
    auto result = L1BlockPredeploy::dispatch(state, makeCall(payload, OP_DEPOSITOR_ACCOUNT));
    BOOST_REQUIRE(result.has_value());
    BOOST_REQUIRE_EQUAL(result->status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(result->output_size, 32);
    evmc_bytes32 output{};
    std::copy(result->output_data, result->output_data + result->output_size, output.bytes);
    if (result->release != nullptr)
    {
        result->release(&result.value());
    }
    return state::fromEvmC(output);
}
}  // namespace

BOOST_AUTO_TEST_CASE(setter_unpacks_isthmus_fixture_into_slots)
{
    state::test::InMemoryStateView baseState;
    state::State state(baseState);
    auto const calldata = loadFixture("isthmus_l1_attributes.bin");

    auto result = L1BlockPredeploy::dispatch(state, makeCall(calldata, OP_DEPOSITOR_ACCOUNT));
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_EQUAL(result->status_code, EVMC_SUCCESS);

    auto const baseFee = state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BASE_FEE_SLOT));
    auto const blobBaseFee =
        state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BLOB_BASE_FEE_SLOT));
    auto const feeScalars =
        state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_FEE_SCALARS_SLOT));
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
    state::test::InMemoryStateView baseState;
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
    state::test::InMemoryStateView baseState;
    state::State state(baseState);
    auto const calldata = loadFixture("isthmus_l1_attributes.bin");

    auto writeResult = L1BlockPredeploy::dispatch(state, makeCall(calldata, OP_DEPOSITOR_ACCOUNT));
    BOOST_REQUIRE(writeResult.has_value());
    BOOST_REQUIRE_EQUAL(writeResult->status_code, EVMC_SUCCESS);

    BOOST_CHECK_EQUAL(callGetter(state, 0x519b4bd3), u256(0x0123456789abcdefULL));
    BOOST_CHECK_EQUAL(callGetter(state, 0xc5985918), u256(0x11223344));
    BOOST_CHECK_EQUAL(callGetter(state, 0x68d5dca6), u256(0x55667788));
    BOOST_CHECK_EQUAL(callGetter(state, 0x84189161), u256(0x0fedcba987654321ULL));
    BOOST_CHECK_EQUAL(callGetter(state, 0x4d5d9a2a), u256(0xa1b2c3d4));
    BOOST_CHECK_EQUAL(callGetter(state, 0x16d3bc7f), u256(0x0102030405060708ULL));
}

}  // namespace bcos::evm::test
