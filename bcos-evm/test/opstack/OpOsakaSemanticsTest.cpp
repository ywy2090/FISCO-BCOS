#include "OpPredeploysSeed.h"
#include "OpTestReceiptFactory.h"
#include "StateDiffWriteback.h"
#include "TestPrinters.h"
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpHost.h>
#include <bcos-evm/opstack/OpPrecompiles.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <bcos-evm/eth/state/precompiles_internal.hpp>
#include <filesystem>
#include <fstream>
#include <test/utils/test_state.hpp>
#include <vector>

using namespace bcos::evm::opstack;
using namespace bcos::evm::opstack::testutil;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
constexpr auto kSender = 0x00000000000000000000000000000000000000aa_address;
constexpr auto kContract = 0x00000000000000000000000000000000000000bb_address;
constexpr auto kModExp = 0x0000000000000000000000000000000000000005_address;
constexpr auto kP256 = 0x0000000000000000000000000000000000000100_address;

std::filesystem::path fixtureDir()
{
    return std::filesystem::path(__FILE__).parent_path() / "fixtures/osaka";
}

std::vector<uint8_t> loadFixture(const char* name)
{
    const auto path = fixtureDir() / name;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    BOOST_REQUIRE_MESSAGE(in, std::string("missing fixture: ") + path.string());
    const auto size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), size);
    BOOST_REQUIRE_MESSAGE(in, std::string("failed to read fixture: ") + path.string());
    return data;
}

state::BlockInfo makeBlock()
{
    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30000000;
    block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;
    return block;
}

struct OpTxRun
{
    bcos::protocol::TransactionReceipt::Ptr receipt;
    evmone::state::StateDiff diff;
};

OpTxRun runOpTx(test::TestState& ts, evmc::VM& vm, const state::Transaction& tx,
    const OpForkConfig& cfg, uint64_t chainId = 1234)
{
    test::TestBlockHashes hashes;
    const auto block = makeBlock();
    OpFeeParams fee{};
    std::vector<uint8_t> env{0x02, 0x11};
    const auto v = opValidate(ts, block, tx, {env.data(), env.size()}, cfg, fee, block.gas_limit);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);
    evmone::state::StateDiff diff;
    auto receipt =
        opTransition(ts, block, hashes, tx, cfg, vm, props, chainId, kOpTestReceiptFactory, diff);
    bcos::evm::applyStateDiffStrict(ts, diff);
    return {std::move(receipt), std::move(diff)};
}

OpTxRun runContractWithCode(
    test::TestState& ts, evmc::VM& vm, evmc::bytes code, const OpForkConfig& cfg)
{
    ts[kSender] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    ts[kContract] = {.nonce = 0, .balance = 0_u256, .storage = {}, .code = std::move(code)};
    seedOpPredeploys(ts);

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kSender;
    tx.to = kContract;
    tx.gas_limit = 5000000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;
    return runOpTx(ts, vm, tx, cfg);
}

struct HostCallResult
{
    evmc_status_code status_code;
    int64_t gas_left;
    std::vector<uint8_t> output;
};

HostCallResult hostCall(evmc_revision rev, const PrecompileOverrides* overrides,
    const evmc::address& target, const std::vector<uint8_t>& input, int64_t gas)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    state::State st{ts};
    test::TestBlockHashes hashes;
    state::Transaction tx;
    tx.sender = kSender;
    const auto block = makeBlock();
    OpHost host{rev, vm, st, block, hashes, tx, 1234, overrides};

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = target;
    msg.code_address = target;
    msg.sender = kSender;
    msg.gas = gas;
    msg.input_data = input.data();
    msg.input_size = input.size();
    const auto r = host.call(msg);
    HostCallResult out{.status_code = r.status_code, .gas_left = r.gas_left};
    if (r.output_data != nullptr && r.output_size > 0)
        out.output.assign(r.output_data, r.output_data + r.output_size);
    return out;
}

std::vector<uint8_t> makeEip7823ModExpInput(size_t baseLen, uint8_t baseByte)
{
    std::vector<uint8_t> input(96 + baseLen + 2, 0x00);
    const auto writeLen = [&](size_t offset, uint64_t len) {
        for (size_t i = 0; i < 8; ++i)
            input[offset + 24 + i] = static_cast<uint8_t>((len >> (56 - i * 8)) & 0xff);
    };
    writeLen(0, baseLen);
    writeLen(32, 1);
    writeLen(64, 1);
    for (size_t i = 0; i < baseLen; ++i)
        input[96 + i] = baseByte;
    input[96 + baseLen] = 0x00;
    input[96 + baseLen + 1] = 0x02;
    return input;
}

state::bytes makeEip7823ModExpData(size_t baseLen, uint8_t baseByte)
{
    const auto raw = makeEip7823ModExpInput(baseLen, baseByte);
    return state::bytes(raw.begin(), raw.end());
}

intx::uint256 readStorageSlot(const test::TestState& ts, const evmc::address& addr, uint64_t slot)
{
    evmc::bytes32 key{};
    if (slot != 0)
        key.bytes[31] = static_cast<uint8_t>(slot);
    return intx::be::load<intx::uint256>(ts.get_storage(addr, key));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpOsakaSemanticsSuite)

BOOST_AUTO_TEST_CASE(ClzVectorsThroughKarstOpPath)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;

    // PUSH0 CLZ PUSH1 0 SSTORE STOP
    const auto r0 =
        runContractWithCode(ts, vm, evmc::bytes{0x5f, 0x1e, 0x60, 0x00, 0x55, 0x00}, karstConfig());
    BOOST_REQUIRE_EQUAL(r0.receipt->status(), 0);
    BOOST_CHECK_EQUAL(readStorageSlot(ts, kContract, 0), intx::uint256{256});

    test::TestState ts1;
    const auto r1 = runContractWithCode(
        ts1, vm, evmc::bytes{0x60, 0x01, 0x1e, 0x60, 0x00, 0x55, 0x00}, karstConfig());
    BOOST_REQUIRE_EQUAL(r1.receipt->status(), 0);
    BOOST_CHECK_EQUAL(readStorageSlot(ts1, kContract, 0), intx::uint256{255});

    test::TestState ts2;
    evmc::bytes push32Clz{0x7f, 0x80};
    for (int i = 0; i < 31; ++i)
        push32Clz.push_back(0x00);
    push32Clz.push_back(0x1e);
    push32Clz.push_back(0x60);
    push32Clz.push_back(0x00);
    push32Clz.push_back(0x55);
    push32Clz.push_back(0x00);
    const auto r2 = runContractWithCode(ts2, vm, std::move(push32Clz), karstConfig());
    BOOST_REQUIRE_EQUAL(r2.receipt->status(), 0);
    BOOST_CHECK_EQUAL(readStorageSlot(ts2, kContract, 0), intx::uint256{0});
}

BOOST_AUTO_TEST_CASE(ClzUndefinedBeforeOsakaOnJovianPath)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    const auto r = runContractWithCode(ts, vm, evmc::bytes{0x5f, 0x1e, 0x00}, jovianConfig());
    BOOST_CHECK_NE(r.receipt->status(), 0);
}

BOOST_AUTO_TEST_CASE(ClzCostsFiveGasUnderOsaka)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState push0Ts;
    const auto push0Run = runContractWithCode(push0Ts, vm, evmc::bytes{0x5f, 0x00}, karstConfig());
    BOOST_REQUIRE_EQUAL(push0Run.receipt->status(), 0);

    test::TestState clzTs;
    const auto clzRun =
        runContractWithCode(clzTs, vm, evmc::bytes{0x5f, 0x1e, 0x00}, karstConfig());
    BOOST_REQUIRE_EQUAL(clzRun.receipt->status(), 0);

    const auto push0Gas = static_cast<int64_t>(push0Run.receipt->gasUsed());
    const auto clzGas = static_cast<int64_t>(clzRun.receipt->gasUsed());
    BOOST_CHECK_EQUAL(clzGas - push0Gas, 5);
}

BOOST_AUTO_TEST_CASE(Eip7823ModExpLengthBoundsThroughKarstOpPath)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    seedOpPredeploys(ts);

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kSender;
    tx.to = kModExp;
    tx.gas_limit = 5000000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;
    tx.data = makeEip7823ModExpData(1024, 0x01);

    const auto ok = runOpTx(ts, vm, tx, karstConfig());
    BOOST_REQUIRE_EQUAL(ok.receipt->status(), 0);

    test::TestState tsFail;
    auto senderCopy = ts.at(kSender);
    senderCopy.nonce = 0;
    tsFail[kSender] = senderCopy;
    seedOpPredeploys(tsFail);
    state::Transaction txFail = tx;
    txFail.nonce = 0;
    txFail.data = makeEip7823ModExpData(1025, 0x01);
    const auto fail = runOpTx(tsFail, vm, txFail, karstConfig());
    BOOST_CHECK_NE(fail.receipt->status(), 0);
    BOOST_CHECK_EQUAL(static_cast<int64_t>(fail.receipt->gasUsed()), txFail.gas_limit);
}

BOOST_AUTO_TEST_CASE(Eip7883ModExpNagydani1GasFromFixture)
{
    const auto input = loadFixture("modexp_nagydani_1_square.input.bin");
    const auto view = evmc::bytes_view{input.data(), input.size()};
    const auto prague = evmone::state::expmod_analyze(view, EVMC_PRAGUE);
    const auto osaka = evmone::state::expmod_analyze(view, EVMC_OSAKA);
    BOOST_CHECK_EQUAL(prague.gas_cost, 200);
    BOOST_CHECK_EQUAL(osaka.gas_cost, 500);
}

BOOST_AUTO_TEST_CASE(Eip7883ModExpNagydani2PowGasFromFixture)
{
    const auto input = loadFixture("modexp_nagydani_2_pow0x10001.input.bin");
    const auto view = evmc::bytes_view{input.data(), input.size()};
    const auto prague = evmone::state::expmod_analyze(view, EVMC_PRAGUE);
    const auto osaka = evmone::state::expmod_analyze(view, EVMC_OSAKA);
    BOOST_CHECK_EQUAL(prague.gas_cost, 1365);
    BOOST_CHECK_EQUAL(osaka.gas_cost, 8192);
}

BOOST_AUTO_TEST_CASE(Eip7951P256VerifyFromFixture)
{
    const auto input = loadFixture("p256verify_valid_input.bin");
    std::vector<uint8_t> expectedSuccess(32, 0x00);
    expectedSuccess[31] = 0x01;

    constexpr int64_t kForwardedGas = 100000;
    const auto jovianOk = hostCall(EVMC_PRAGUE, &jovianPrecompileOverrides(), kP256, input, 3450);
    BOOST_CHECK_EQUAL(jovianOk.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(jovianOk.gas_left, 0);
    BOOST_CHECK_EQUAL(jovianOk.output.size(), 32);
    BOOST_CHECK_EQUAL_COLLECTIONS(jovianOk.output.begin(), jovianOk.output.end(),
        expectedSuccess.begin(), expectedSuccess.end());

    const auto jovianFail = hostCall(EVMC_PRAGUE, &jovianPrecompileOverrides(), kP256, input, 3449);
    BOOST_CHECK_EQUAL(jovianFail.status_code, EVMC_OUT_OF_GAS);

    const auto karstOk = hostCall(EVMC_OSAKA, &karstPrecompileOverrides(), kP256, input, 6900);
    BOOST_CHECK_EQUAL(karstOk.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(karstOk.gas_left, 0);
    BOOST_CHECK_EQUAL(karstOk.output.size(), 32);
    BOOST_CHECK_EQUAL_COLLECTIONS(karstOk.output.begin(), karstOk.output.end(),
        expectedSuccess.begin(), expectedSuccess.end());

    const auto karstFail = hostCall(EVMC_OSAKA, &karstPrecompileOverrides(), kP256, input, 6899);
    BOOST_CHECK_EQUAL(karstFail.status_code, EVMC_OUT_OF_GAS);
}

BOOST_AUTO_TEST_SUITE_END()
