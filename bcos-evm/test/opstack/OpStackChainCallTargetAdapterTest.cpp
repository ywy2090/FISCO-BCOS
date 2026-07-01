#define BOOST_TEST_MODULE OpStackChainCallTargetAdapterTest

#include "bcos-evm/opstack/adapter/OpStackChainCallTargetAdapter.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/core/CallTargetKind.h"
#include "bcos-evm/eth/kernel/execution/CallTargetResolver.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <cstring>
#include <set>
#include <vector>

namespace bcos::evm::test
{
namespace
{

evmc_message makeCall(evmc_address target)
{
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = 500'000;
    msg.recipient = target;
    msg.code_address = target;
    return msg;
}

}  // namespace

BOOST_AUTO_TEST_CASE(classify_l1_block_and_gas_oracle)
{
    state::test::InMemoryStateView base;
    state::State state{base};
    OpStackChainCallTargetAdapter adapter(&state, bcos::u256(0), makeIsthmusPlusForkSchedule(), 0);

    auto checkClassify = [&](evmc_address const& target) {
        auto desc = adapter.classifyTarget(
            state, target, makeCall(target), execution::FrameScope::TopLevel);
        BOOST_REQUIRE(desc.has_value());
        BOOST_CHECK(desc->kind == execution::CallTargetKind::ChainPrecompile);
        BOOST_CHECK(desc->warmPolicy == execution::WarmPolicy::TxEntryIfStatic);
        BOOST_CHECK(
            std::memcmp(desc->dispatchAddress.bytes, target.bytes, sizeof(target.bytes)) == 0);
    };

    checkClassify(OP_L1_BLOCK_PREDEPLOY);
    checkClassify(OP_GAS_PRICE_ORACLE_PREDEPLOY);

    evmc_address unrelated{};
    unrelated.bytes[19] = 0x01;
    BOOST_CHECK(
        !adapter
             .classifyTarget(state, unrelated, makeCall(unrelated), execution::FrameScope::TopLevel)
             .has_value());
}

BOOST_AUTO_TEST_CASE(enumerate_static_warm_targets)
{
    OpStackChainCallTargetAdapter adapter(nullptr, bcos::u256(0), makeIsthmusPlusForkSchedule(), 0);

    std::vector<evmc_address> warmed;
    adapter.forEachStaticWarmTarget([&](evmc_address const& addr) { warmed.push_back(addr); });

    auto contains = [&](evmc_address const& needle) {
        return std::any_of(warmed.begin(), warmed.end(), [&](evmc_address const& a) {
            return std::memcmp(a.bytes, needle.bytes, sizeof(needle.bytes)) == 0;
        });
    };

    BOOST_REQUIRE_EQUAL(warmed.size(), 2U);
    BOOST_CHECK(contains(OP_L1_BLOCK_PREDEPLOY));
    BOOST_CHECK(contains(OP_GAS_PRICE_ORACLE_PREDEPLOY));

    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_CANCUN};
    std::set<uint8_t> builtinLowBytes;
    execution::enumerateTxEntryWarmTargets(cfg, &adapter, [&](evmc_address const& a) {
        if (a.bytes[0] == 0 && a.bytes[18] == 0 && a.bytes[17] == 0)
        {
            builtinLowBytes.insert(a.bytes[19]);
        }
    });
    BOOST_CHECK(builtinLowBytes.count(0x01) > 0);
    BOOST_CHECK(contains(OP_L1_BLOCK_PREDEPLOY));
    BOOST_CHECK(contains(OP_GAS_PRICE_ORACLE_PREDEPLOY));
}

BOOST_AUTO_TEST_CASE(invariant_classify_warm_policy_matches_static_warm_enumerate)
{
    state::test::InMemoryStateView base;
    state::State state{base};
    OpStackChainCallTargetAdapter adapter(&state, bcos::u256(0), makeIsthmusPlusForkSchedule(), 0);

    std::vector<evmc_address> enumerated;
    adapter.forEachStaticWarmTarget([&](evmc_address const& addr) { enumerated.push_back(addr); });

    auto const expected = {OP_L1_BLOCK_PREDEPLOY, OP_GAS_PRICE_ORACLE_PREDEPLOY};
    BOOST_REQUIRE_EQUAL(enumerated.size(), expected.size());

    for (auto const& target : expected)
    {
        auto desc = adapter.classifyTarget(
            state, target, makeCall(target), execution::FrameScope::TopLevel);
        BOOST_REQUIRE(desc.has_value());
        BOOST_CHECK(desc->kind == execution::CallTargetKind::ChainPrecompile);
        BOOST_CHECK(execution::isTxEntryWarm(desc->warmPolicy));

        auto inEnumerate =
            std::any_of(enumerated.begin(), enumerated.end(), [&](evmc_address const& a) {
                return std::memcmp(a.bytes, target.bytes, sizeof(target.bytes)) == 0;
            });
        BOOST_CHECK(inEnumerate);
    }
}

}  // namespace bcos::evm::test
