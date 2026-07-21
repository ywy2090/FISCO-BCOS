#include <bcos-evm/adapter/StateDiffWriteback.h>
#include <bcos-evm/adapter/StateViewAdapter.h>
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <bcos-evm/eth/state/host.hpp>  // compute_create_address
#include <bcos-evm/eth/state/state.hpp>
#include <bcos-evm/eth/utils/test_state.hpp>

static_assert(std::is_abstract_v<bcos::evmref::StateView>);

using namespace evmone;
using namespace evmc::literals;
using namespace intx::literals;

namespace
{
constexpr auto kSender = 0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b_address;
constexpr int64_t kCancunBlobGasLeft = 786432;  // 6 blobs * 131072 (EIP-4844)
// NOTE: 原始数字分隔符写法 1'000'...'_u256 在本仓库 vcpkg 锁定的 intx 0.15.0 下编译失败：
// intx::from_string 的 from_dec_digit 不识别 '\'' 分隔符（consteval 求值直接抛错，
// 见 intx.hpp from_dec_digit/from_string）。数值不变（仍为 1e18 wei），仅去除分隔符使其可编译。
constexpr auto kFunding = 1000000000000000000_u256;  // 1 ETH in wei

state::BlockInfo makeBlock()
{
    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30'000'000;
    block.base_fee = 7;
    block.coinbase = 0x00000000000000000000000000000000c014ba5e_address;
    return block;
}

state::Transaction makeTx()
{
    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kSender;
    tx.gas_limit = 200'000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;
    return tx;
}

// validate + transition；校验失败时报告并返回 nullopt。diff 不写回。
std::optional<state::TransactionReceipt> run(const test::TestState& pre, const state::Transaction& tx)
{
    const auto block = makeBlock();
    test::TestBlockHashes hashes;
    evmc::VM vm{evmc_create_evmone()};
    const auto validated = state::validate_transaction(
        pre, block, tx, EVMC_CANCUN, block.gas_limit, kCancunBlobGasLeft);
    if (const auto* err = std::get_if<std::error_code>(&validated))
    {
        BOOST_ERROR("validate: " + err->message());
        return std::nullopt;
    }
    return state::transition(pre, block, hashes, tx, EVMC_CANCUN, vm,
        std::get<state::TransactionProperties>(validated));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(StateDiffWritebackTest)

// ============ 缝契约测试（手工构造 diff：未来真账本写回实现的规约） ============

BOOST_AUTO_TEST_CASE(ContractDeletesListedAccount)
{
    const auto victim = 0x00000000000000000000000000000000dead0001_address;
    test::TestState state;
    state[victim].nonce = 1;
    state[victim].balance = 7;
    state[victim].storage[0x01_bytes32] = 0x02_bytes32;
    BOOST_REQUIRE_EQUAL(state.count(victim), 1u);  // 前置断言：防空转假绿

    state::StateDiff diff;
    diff.deleted_accounts.push_back(victim);
    bcos::evmref::applyStateDiff(state, diff);

    BOOST_CHECK_EQUAL(state.count(victim), 0u);
}

BOOST_AUTO_TEST_CASE(ContractStorageZeroMeansErase)
{
    const auto acct = 0x00000000000000000000000000000000dead0002_address;
    const auto k1 = 0x01_bytes32;
    const auto k2 = 0x02_bytes32;
    const auto k3 = 0x03_bytes32;
    test::TestState state;
    state[acct].nonce = 1;  // balance 缺省 0
    state[acct].storage[k2] = 0xaa_bytes32;
    state[acct].storage[k3] = 0xcc_bytes32;

    state::StateDiff diff;
    auto& entry = diff.modified_accounts.emplace_back();
    entry.addr = acct;
    entry.nonce = 1;
    entry.balance = 0;
    entry.code = std::nullopt;  // 不动 code
    entry.modified_storage = {
        {k1, 0x0b_bytes32},  // 新写入
        {k2, {}},            // 置 0 = 删除该槽
    };
    bcos::evmref::applyStateDiff(state, diff);

    BOOST_CHECK(state.at(acct).storage.at(k1) == 0x0b_bytes32);
    BOOST_CHECK_EQUAL(state.at(acct).storage.count(k2), 0u);  // erase 而非存零（真账本最易做错的一条）
    BOOST_CHECK(state.at(acct).storage.at(k3) == 0xcc_bytes32);  // 未触及槽必须存活（merge 而非 replace）
}

BOOST_AUTO_TEST_CASE(ContractNulloptCodePreservesExisting)
{
    const auto acct = 0x00000000000000000000000000000000dead0003_address;
    test::TestState state;
    state[acct].nonce = 1;  // balance 缺省 0
    state[acct].code = *evmc::from_hex("6001600155");  // 任意非空字节码
    const auto originalCode = state.at(acct).code;

    state::StateDiff diff;
    auto& entry = diff.modified_accounts.emplace_back();
    entry.addr = acct;
    entry.nonce = 1;
    entry.balance = 42;               // 仅改余额
    entry.code = std::nullopt;        // 契约：不得动 code
    bcos::evmref::applyStateDiff(state, diff);

    BOOST_CHECK(state.at(acct).balance == 42);
    BOOST_CHECK(state.at(acct).code == originalCode);  // code 必须原样保留
}

// ============ 语义发现测试（真实交易全链路产生 diff） ============

// EIP-6780: 合约在创建它的同一笔交易内 SELFDESTRUCT -> 进 deleted_accounts。
// tx.value 必须为 1：value=0 时 beneficiary 被 touch 后保持空账户，
// 也会以 EIP-161 分支进 deleted_accounts（列表变 2 项且顺序不定）。
BOOST_AUTO_TEST_CASE(DeletesSameTxSelfdestruct)
{
    test::TestState pre;
    pre[kSender].balance = kFunding;  // nonce 缺省 0

    auto tx = makeTx();
    tx.to = {};    // 合约创建
    tx.value = 1;  // 使 beneficiary 非空，删除项唯一
    // initcode: PUSH20 0x...beef ; SELFDESTRUCT
    tx.data = *evmc::from_hex("73" "000000000000000000000000000000000000beef" "ff");

    const auto receipt = run(pre, tx);
    BOOST_REQUIRE(receipt.has_value());
    BOOST_REQUIRE(receipt->status == EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(receipt->state_diff.deleted_accounts.size(), 1u);
    BOOST_CHECK(receipt->state_diff.deleted_accounts[0] ==
                state::compute_create_address(kSender, 0));

    bcos::evmref::applyStateDiff(pre, receipt->state_diff);
    BOOST_CHECK_EQUAL(pre.count(receipt->state_diff.deleted_accounts[0]), 0u);
}

// EIP-161: pre-state 预置空账户被零值转账触碰 -> 擦除
BOOST_AUTO_TEST_CASE(ErasesTouchedEmptyAccount)
{
    const auto empty = 0x00000000000000000000000000000000c0ffee00_address;
    test::TestState pre;
    pre[kSender].balance = kFunding;  // nonce 缺省 0
    pre[empty] = {};  // 空账户：nonce=0, balance=0, 无 code

    auto tx = makeTx();
    tx.to = empty;
    tx.value = 0;

    const auto receipt = run(pre, tx);
    BOOST_REQUIRE(receipt.has_value());
    BOOST_REQUIRE(receipt->status == EVMC_SUCCESS);
    BOOST_REQUIRE(!receipt->state_diff.deleted_accounts.empty());

    BOOST_REQUIRE_EQUAL(pre.count(empty), 1u);  // 前置断言：防空转假绿
    bcos::evmref::applyStateDiff(pre, receipt->state_diff);
    BOOST_CHECK_EQUAL(pre.count(empty), 0u);
}

BOOST_AUTO_TEST_SUITE_END()
