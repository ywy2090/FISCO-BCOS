#define BOOST_TEST_MODULE CallTargetResolverTest

#include "bcos-evm/eth/kernel/execution/CallTargetResolver.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/kernel/execution/FrameRouting.h"
#include "bcos-evm/eth/kernel/execution/FrameScope.h"
#include "bcos-evm/eth/precompiled/PrecompileActive.h"
#include "bcos/adapters/InMemoryChainCallTargetAdapter.h"
#include "fixtures/EthFrameParityHelpers.h"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace bcos::evm::test
{
namespace
{

evmc_address precompileAddr(uint8_t low)
{
    evmc_address a{};
    a.bytes[19] = low;
    return a;
}

evmc_address addressFromValue(uint64_t value)
{
    evmc_address address{};
    for (int i = 19; i >= 0 && value > 0; --i)
    {
        address.bytes[i] = static_cast<uint8_t>(value & 0xFF);
        value >>= 8U;
    }
    return address;
}

bcos::evm::RevisionConfig pragueCfg()
{
    return {.revision = EVMC_PRAGUE, .eip2929 = true, .eip2537 = true, .eip7702 = true};
}

execution::ClassifiedCallTarget resolveAt(state::State& state, bcos::evm::RevisionConfig const& cfg,
    evmc_message msg, execution::FrameScope scope, ChainCallTargetPort* callTargetPort = nullptr,
    state::EvmHostHooks* extension = nullptr)
{
    auto frame = execution::routeFrameMessage(state, cfg, msg, scope);
    return execution::classifyCallTarget(
        state, cfg, frame.routed, scope, callTargetPort, extension);
}

struct DenyDelegatePrecompilePolicy : state::EvmHostHooks
{
    bool allowDelegateCallToPrecompile() override { return false; }
};

}  // namespace

BOOST_AUTO_TEST_CASE(R1_empty_code_active_builtin)
{
    state::test::InMemoryStateView base;
    state::State state{base};
    bcos::evm::RevisionConfig cfg{.revision = EVMC_PRAGUE, .eip2537 = true};

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = precompileAddr(0x04);
    msg.code_address = msg.recipient;
    msg.gas = 100000;

    auto desc = resolveAt(state, cfg, msg, execution::FrameScope::TopLevel);

    BOOST_CHECK(desc.route == execution::CallTargetRoute::BuiltinPrecompile);
    BOOST_CHECK(desc.accessWarm == execution::AccessWarmSchedule::AtTxPrepare);
    BOOST_CHECK(std::memcmp(desc.dispatchAddress.bytes, msg.recipient.bytes,
                    sizeof(msg.recipient.bytes)) == 0);
}

BOOST_AUTO_TEST_CASE(R2_inactive_precompile_empty_account)
{
    auto const bls = precompileAddr(0x0b);
    state::test::InMemoryStateView base;
    state::State state{base};

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = bls;
    msg.code_address = bls;
    msg.gas = 50000;

    bcos::evm::RevisionConfig cfg{.revision = EVMC_CANCUN};
    BOOST_REQUIRE(!precompiled::isActivePrecompile(cfg, bls));

    auto desc = resolveAt(state, cfg, msg, execution::FrameScope::TopLevel);

    BOOST_CHECK(desc.route == execution::CallTargetRoute::EmptyAccount);
    BOOST_CHECK(desc.accessWarm == execution::AccessWarmSchedule::AtFirstAccess);
}

BOOST_AUTO_TEST_CASE(R3_chain_classify_empty_code_toplevel)
{
    auto const chainAddr = addressFromValue(0x1000);
    state::test::InMemoryStateView base;
    state::State state{base};

    InMemoryChainCallTargetAdapter adapter(
        [&](state::State&, evmc_address const& executionAddress, evmc_message const&,
            execution::FrameScope) -> std::optional<execution::ClassifiedCallTarget> {
            return execution::ClassifiedCallTarget{
                .route = execution::CallTargetRoute::ChainPrecompile,
                .dispatchAddress = executionAddress,
                .accessWarm = execution::AccessWarmSchedule::AtFirstAccess,
            };
        });

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = chainAddr;
    msg.code_address = chainAddr;
    msg.gas = 50000;

    auto desc = resolveAt(state, pragueCfg(), msg, execution::FrameScope::TopLevel, &adapter);

    BOOST_CHECK(desc.route == execution::CallTargetRoute::ChainPrecompile);
    BOOST_CHECK(
        std::memcmp(desc.dispatchAddress.bytes, chainAddr.bytes, sizeof(chainAddr.bytes)) == 0);
}

BOOST_AUTO_TEST_CASE(R4_chain_precompile_wins_over_active_builtin)
{
    auto const identity = precompileAddr(0x04);
    state::test::InMemoryStateView base;
    state::State state{base};

    InMemoryChainCallTargetAdapter adapter(
        [&](state::State&, evmc_address const& executionAddress, evmc_message const&,
            execution::FrameScope) -> std::optional<execution::ClassifiedCallTarget> {
            return execution::ClassifiedCallTarget{
                .route = execution::CallTargetRoute::ChainPrecompile,
                .dispatchAddress = executionAddress,
                .accessWarm = execution::AccessWarmSchedule::AtFirstAccess,
            };
        });

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = 50000;
    msg.recipient = identity;
    msg.code_address = identity;

    bcos::evm::RevisionConfig cfg{.revision = EVMC_PRAGUE, .eip2537 = true};
    BOOST_REQUIRE(precompiled::isActivePrecompile(cfg, identity));

    auto desc = resolveAt(state, cfg, msg, execution::FrameScope::TopLevel, &adapter);

    BOOST_CHECK(desc.route == execution::CallTargetRoute::ChainPrecompile);
    BOOST_CHECK(desc.route != execution::CallTargetRoute::BuiltinPrecompile);
}

BOOST_AUTO_TEST_CASE(R5d_7702_evmc_delegated_staticcall_to_precompile_is_empty_account)
{
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.flags = EVMC_DELEGATED | EVMC_STATIC;
    msg.gas = 0;
    msg.recipient = addressFromLastByte(0x02);
    msg.code_address = precompileAddr(0x01);

    state::test::InMemoryStateView base;
    state::State state{base};

    auto desc = resolveAt(state, pragueCfg(), msg, execution::FrameScope::Nested);

    BOOST_CHECK(desc.route == execution::CallTargetRoute::EmptyAccount);
}

BOOST_AUTO_TEST_CASE(R5c_7702_evmc_delegated_delegatecall_to_precompile_is_empty_account)
{
    evmc_message msg{};
    msg.kind = EVMC_DELEGATECALL;
    msg.flags = EVMC_DELEGATED;
    msg.gas = 0;
    msg.recipient = addressFromLastByte(0x02);
    msg.code_address = precompileAddr(0x01);

    state::test::InMemoryStateView base;
    state::State state{base};

    auto desc = resolveAt(state, pragueCfg(), msg, execution::FrameScope::Nested);

    BOOST_CHECK(desc.route == execution::CallTargetRoute::EmptyAccount);
}

BOOST_AUTO_TEST_CASE(R5b_7702_delegation_to_precompile_delegatecall_is_empty_account)
{
    auto const authority = addressFromLastByte(0xAA);
    auto const identity = precompileAddr(0x01);
    auto delegationCode = addressToDelegation(identity);

    state::test::InMemoryStateView base;
    state::State state{base};
    state.set_code(authority, delegationCode,
        state::keccak256Code(bcos::bytesConstRef{delegationCode.data(), delegationCode.size()}));

    evmc_message msg{};
    msg.kind = EVMC_DELEGATECALL;
    msg.gas = 0;
    msg.recipient = addressFromLastByte(0x02);
    msg.code_address = authority;

    auto desc = resolveAt(state, pragueCfg(), msg, execution::FrameScope::Nested);

    BOOST_CHECK(desc.route == execution::CallTargetRoute::EmptyAccount);
    BOOST_CHECK(
        std::memcmp(desc.dispatchAddress.bytes, authority.bytes, sizeof(authority.bytes)) == 0);
}

BOOST_AUTO_TEST_CASE(R5_7702_delegation_designator_is_evm_contract)
{
    auto const authority = addressFromLastByte(0xAA);
    auto const identity = precompileAddr(0x04);
    auto delegationCode = addressToDelegation(identity);

    state::test::InMemoryStateView base;
    state::State state{base};
    state.set_code(authority, delegationCode,
        state::keccak256Code(bcos::bytesConstRef{delegationCode.data(), delegationCode.size()}));

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.flags = EVMC_DELEGATED;
    msg.recipient = authority;
    msg.code_address = identity;

    auto desc = resolveAt(state, pragueCfg(), msg, execution::FrameScope::TopLevel);

    BOOST_CHECK(desc.route == execution::CallTargetRoute::EvmContract);
    BOOST_CHECK(
        std::memcmp(desc.dispatchAddress.bytes, authority.bytes, sizeof(authority.bytes)) == 0);
}

BOOST_AUTO_TEST_CASE(R6_delegatecall_to_precompile_policy_rejected)
{
    auto const caller = addressFromLastByte(0x02);
    auto const identity = precompileAddr(0x04);

    evmc_message msg{};
    msg.kind = EVMC_DELEGATECALL;
    msg.gas = 50000;
    msg.recipient = caller;
    msg.code_address = identity;

    DenyDelegatePrecompilePolicy policy;
    state::test::InMemoryStateView base;
    state::State state{base};

    auto desc = resolveAt(state, pragueCfg(), msg, execution::FrameScope::Nested, nullptr, &policy);

    BOOST_CHECK(desc.route == execution::CallTargetRoute::BuiltinPrecompile);
    BOOST_CHECK(desc.admission == execution::CallTargetAdmission::DenyDelegateCallPrecompile);
    BOOST_CHECK(desc.accessWarm == execution::AccessWarmSchedule::AtFirstAccess);
}

BOOST_AUTO_TEST_CASE(R7_chain_proxy_toplevel_empty_and_nested_marker)
{
    auto const chainDirect = addressFromValue(0x1000);
    auto const markerContract = addressFromValue(0x2222);
    auto const resolvedTarget = addressFromValue(0x1003);
    auto const markerCode = std::string("[PRECOMPILED],0000000000000000000000000000000000001003");

    state::test::InMemoryStateView base;
    state::State state{base};
    state.set_code(markerContract, bcos::bytes(markerCode.begin(), markerCode.end()), {});

    InMemoryChainCallTargetAdapter adapter(
        [&](state::State& s, evmc_address const& executionAddress, evmc_message const&,
            execution::FrameScope scope) -> std::optional<execution::ClassifiedCallTarget> {
            if (scope == execution::FrameScope::TopLevel &&
                std::memcmp(executionAddress.bytes, chainDirect.bytes, sizeof(chainDirect.bytes)) ==
                    0)
            {
                return execution::ClassifiedCallTarget{
                    .route = execution::CallTargetRoute::ChainPrecompile,
                    .dispatchAddress = executionAddress,
                    .accessWarm = execution::AccessWarmSchedule::AtFirstAccess,
                };
            }
            if (scope == execution::FrameScope::Nested)
            {
                auto const code = s.get_code(executionAddress);
                auto const codeView =
                    std::string_view{reinterpret_cast<char const*>(code.data()), code.size()};
                if (codeView.rfind("[PRECOMPILED]", 0) == 0)
                {
                    return execution::ClassifiedCallTarget{
                        .route = execution::CallTargetRoute::ChainPrecompile,
                        .dispatchAddress = resolvedTarget,
                        .accessWarm = execution::AccessWarmSchedule::AtFirstAccess,
                    };
                }
            }
            return std::nullopt;
        });

    evmc_message topMsg{};
    topMsg.kind = EVMC_CALL;
    topMsg.gas = 50000;
    topMsg.recipient = chainDirect;
    topMsg.code_address = chainDirect;

    auto topDesc = resolveAt(state, pragueCfg(), topMsg, execution::FrameScope::TopLevel, &adapter);
    BOOST_CHECK(topDesc.route == execution::CallTargetRoute::ChainPrecompile);

    evmc_message nestedMsg{};
    nestedMsg.kind = EVMC_CALL;
    nestedMsg.gas = 50000;
    nestedMsg.recipient = markerContract;
    nestedMsg.code_address = markerContract;

    auto nestedDesc =
        resolveAt(state, pragueCfg(), nestedMsg, execution::FrameScope::Nested, &adapter);
    BOOST_CHECK(nestedDesc.route == execution::CallTargetRoute::ChainPrecompile);
    BOOST_CHECK(std::memcmp(nestedDesc.dispatchAddress.bytes, resolvedTarget.bytes,
                    sizeof(resolvedTarget.bytes)) == 0);
}

BOOST_AUTO_TEST_CASE(R8_create_kind_returns_evm_contract)
{
    auto const precompile = precompileAddr(0x04);
    state::test::InMemoryStateView base;
    state::State state{base};

    evmc_message msg{};
    msg.kind = EVMC_CREATE;
    msg.gas = 100000;
    msg.sender = addressFromLastByte(0x01);
    msg.recipient = precompile;
    msg.code_address = precompile;

    auto desc = resolveAt(state, pragueCfg(), msg, execution::FrameScope::TopLevel);

    BOOST_CHECK(desc.route == execution::CallTargetRoute::EvmContract);
    BOOST_CHECK(desc.accessWarm == execution::AccessWarmSchedule::AtFirstAccess);
}

BOOST_AUTO_TEST_CASE(W1_enumerate_active_builtin_precompiles)
{
    bcos::evm::RevisionConfig cfg{.revision = EVMC_CANCUN};
    std::set<uint8_t> warmedLowBytes;
    execution::enumerateTxEntryWarmTargets(
        cfg, nullptr, [&](evmc_address const& a) { warmedLowBytes.insert(a.bytes[19]); });

    BOOST_CHECK(warmedLowBytes.count(0x01) > 0);
    BOOST_CHECK(warmedLowBytes.count(0x04) > 0);
    BOOST_CHECK(warmedLowBytes.count(0x0a) > 0);
    BOOST_CHECK(warmedLowBytes.count(0x0b) == 0);
}

BOOST_AUTO_TEST_CASE(W2_enumerate_chain_static_warm_targets)
{
    evmc_address l1Block{};
    l1Block.bytes[18] = 0x42;
    l1Block.bytes[19] = 0x0A;
    evmc_address gasOracle{};
    gasOracle.bytes[18] = 0x42;
    gasOracle.bytes[19] = 0x0F;

    InMemoryChainCallTargetAdapter adapter({}, {});
    adapter.addStaticWarmTarget(l1Block);
    adapter.addStaticWarmTarget(gasOracle);

    bcos::evm::RevisionConfig cfg{.revision = EVMC_CANCUN};
    std::vector<evmc_address> warmed;
    execution::enumerateTxEntryWarmTargets(
        cfg, &adapter, [&](evmc_address const& a) { warmed.push_back(a); });

    auto contains = [&](evmc_address const& needle) {
        return std::any_of(warmed.begin(), warmed.end(), [&](evmc_address const& a) {
            return std::memcmp(a.bytes, needle.bytes, sizeof(needle.bytes)) == 0;
        });
    };

    BOOST_CHECK(contains(l1Block));
    BOOST_CHECK(contains(gasOracle));
    BOOST_CHECK(contains(precompileAddr(0x01)));
}

}  // namespace bcos::evm::test
