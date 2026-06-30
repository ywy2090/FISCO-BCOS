#include "bcos-evm/bcos/FiscoPolicy.h"
#include "bcos-framework/ledger/Features.h"
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::chain_policy;
using Flag = bcos::ledger::Features::Flag;

BOOST_AUTO_TEST_SUITE(FiscoPolicyTest)

ledger::Features makeFeatures()
{
    ledger::Features f;
    f.set(Flag::feature_evm_cancun);
    f.set(Flag::feature_evm_prague);
    f.set(Flag::feature_evm_osaka);
    f.set(Flag::feature_evm_eip2929);
    f.set(Flag::bugfix_v1_error_handling);
    f.set(Flag::bugfix_delegatecall_transfer);
    f.set(Flag::bugfix_auth_check);
    f.set(Flag::bugfix_nonce_initialize);
    f.set(Flag::bugfix_revert_logs);
    f.set(Flag::bugfix_gas_payment_balance_precheck);
    f.set(Flag::bugfix_evm_storage_status);
    f.set(Flag::bugfix_precompiled_feature_gate);
    f.set(Flag::feature_raw_address);
    f.set(Flag::bugfix_v1_timestamp);
    f.set(Flag::feature_evm_timestamp);
    return f;
}

BOOST_AUTO_TEST_CASE(computeRevisionConfigAllFlagsOn)
{
    auto features = makeFeatures();
    FiscoPolicy policy(features, true, true);

    bcostars::protocol::BlockHeaderImpl header(
        [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
    header.setVersion(static_cast<uint32_t>(protocol::BlockVersion::V3_2_VERSION));

    auto rev = policy.computeRevisionConfig(header);
    auto const& ethRev = rev.eth();

    BOOST_CHECK(ethRev.eip2929);
    BOOST_CHECK(ethRev.eip2537);
    BOOST_CHECK(ethRev.eip7212);
    BOOST_CHECK(ethRev.eip7623);
    BOOST_CHECK(ethRev.eip7823);
    BOOST_CHECK(rev.fix_error_handling);
    BOOST_CHECK(rev.fix_delegatecall_transfer);
    BOOST_CHECK(rev.fix_auth_check);
    BOOST_CHECK(rev.fix_nonce_init);
    BOOST_CHECK(rev.fix_revert_logs);
    BOOST_CHECK(rev.fix_gas_precheck);
    BOOST_CHECK(rev.fix_storage_status);
    BOOST_CHECK(rev.fix_precompiled_feature_gate);
    BOOST_CHECK(rev.use_raw_address);
    BOOST_CHECK(rev.use_web3_timestamp);
    BOOST_CHECK(rev.enable_balance_transfer);
    BOOST_CHECK(rev.enable_auth_check);
    BOOST_CHECK_EQUAL(ethRev.calldata_floor_per_token, 10);
}

BOOST_AUTO_TEST_CASE(computeRevisionConfigAllFlagsOff)
{
    ledger::Features features;
    FiscoPolicy policy(features, false, false);

    bcostars::protocol::BlockHeaderImpl header(
        [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
    header.setVersion(0);

    auto rev = policy.computeRevisionConfig(header);
    auto const& ethRev = rev.eth();

    BOOST_CHECK(!ethRev.eip2929);
    BOOST_CHECK(ethRev.eip1153);
    BOOST_CHECK(ethRev.eip4844);
    BOOST_CHECK(ethRev.eip5656);
    BOOST_CHECK(ethRev.eip6780);
    BOOST_CHECK(!ethRev.eip2537);
    BOOST_CHECK(!ethRev.eip7212);
    BOOST_CHECK(!ethRev.eip7623);
    BOOST_CHECK(!ethRev.eip7823);
    BOOST_CHECK(!rev.fix_error_handling);
    BOOST_CHECK(!rev.fix_delegatecall_transfer);
    BOOST_CHECK(!rev.fix_auth_check);
    BOOST_CHECK(!rev.fix_nonce_init);
    BOOST_CHECK(!rev.fix_revert_logs);
    BOOST_CHECK(!rev.fix_gas_precheck);
    BOOST_CHECK(!rev.fix_storage_status);
    BOOST_CHECK(!rev.use_raw_address);
    BOOST_CHECK(!rev.use_web3_timestamp);
    BOOST_CHECK(!rev.enable_balance_transfer);
    BOOST_CHECK(!rev.enable_auth_check);
    BOOST_CHECK_EQUAL(ethRev.calldata_floor_per_token, 0);
}

BOOST_AUTO_TEST_CASE(allowDelegateCallToPrecompile)
{
    ledger::Features features;
    FiscoPolicy policy(features, false, false);
    BOOST_CHECK(!policy.allowDelegateCallToPrecompile());
}

BOOST_AUTO_TEST_SUITE_END()
