#define BOOST_TEST_MODULE RevisionConfigProfileTest

#include "bcos-evm/bcos/FiscoPolicy.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/vm/EthPolicy.h"
#include <bcos-framework/ledger/Features.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <boost/test/included/unit_test.hpp>
#include <functional>
#include <vector>

using namespace bcos;
using namespace bcos::evm_standard;

namespace
{
bcostars::protocol::BlockHeaderImpl makeHeader(int64_t number, uint32_t version)
{
    auto holder = std::make_shared<bcostars::BlockHeader>();
    bcostars::protocol::BlockHeaderImpl header([holder]() { return holder.get(); });
    header.setNumber(number);
    header.setVersion(version);
    return header;
}

struct ExpectedRevisionConfig
{
    evmc_revision revision = EVMC_CANCUN;
#define REVISION_CONFIG_FIELD(name) bool name = false;
    REVISION_CONFIG_BOOL_FIELDS(REVISION_CONFIG_FIELD)
#undef REVISION_CONFIG_FIELD
    uint8_t calldata_floor_per_token = 0;
};

inline void assertRevisionConfigMatches(
    bcos::evm_standard::RevisionConfig const& actual, ExpectedRevisionConfig const& expected)
{
    BOOST_CHECK_EQUAL(actual.revision, expected.revision);
#define REVISION_CONFIG_ASSERT(name) BOOST_CHECK_EQUAL(actual.name, expected.name);
    REVISION_CONFIG_BOOL_FIELDS(REVISION_CONFIG_ASSERT)
#undef REVISION_CONFIG_ASSERT
    BOOST_CHECK_EQUAL(actual.calldata_floor_per_token, expected.calldata_floor_per_token);
}

inline void assertIsthmusHelperProfile(bcos::evm_standard::RevisionConfig const& actual)
{
    ExpectedRevisionConfig expected{};
    expected.revision = EVMC_PRAGUE;
    expected.warm_access = true;
    expected.eip7623 = true;
    expected.eip7702 = true;
    expected.eip4844 = true;
    expected.prague_post_execution = false;
    expected.calldata_floor_per_token = 10;
    assertRevisionConfigMatches(actual, expected);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(RevisionConfigProfileTest)

BOOST_AUTO_TEST_CASE(revision_config_bool_field_macro_count)
{
    BOOST_CHECK_EQUAL(revisionConfigBoolFieldCount(), 13U);
}

BOOST_AUTO_TEST_CASE(derive_canonical_full_fork_snapshots)
{
    struct Row
    {
        evmc_revision revision;
        ExpectedRevisionConfig expected;
    };
    std::vector<Row> const rows = {
        {EVMC_LONDON, {.revision = EVMC_LONDON, .warm_access = true, .eip1559 = true}},
        {EVMC_PARIS, {.revision = EVMC_PARIS, .warm_access = true, .eip1559 = true}},
        {EVMC_SHANGHAI,
            {.revision = EVMC_SHANGHAI, .warm_access = true, .eip1559 = true, .eip3651 = true}},
        {EVMC_CANCUN, {.revision = EVMC_CANCUN,
                          .warm_access = true,
                          .eip1153 = true,
                          .eip4844 = true,
                          .eip5656 = true,
                          .eip6780 = true,
                          .eip1559 = true,
                          .eip3651 = true}},
        {EVMC_PRAGUE, {.revision = EVMC_PRAGUE,
                          .warm_access = true,
                          .eip2537 = true,
                          .eip7623 = true,
                          .eip1153 = true,
                          .eip4844 = true,
                          .eip5656 = true,
                          .eip6780 = true,
                          .eip1559 = true,
                          .eip3651 = true,
                          .eip7702 = true,
                          .calldata_floor_per_token = 10}},
        {EVMC_OSAKA, {.revision = EVMC_OSAKA,
                         .warm_access = true,
                         .eip2537 = true,
                         .eip7212 = true,
                         .eip7623 = true,
                         .eip7823 = true,
                         .eip1153 = true,
                         .eip4844 = true,
                         .eip5656 = true,
                         .eip6780 = true,
                         .eip1559 = true,
                         .eip3651 = true,
                         .eip7702 = true,
                         .calldata_floor_per_token = 10}},
    };
    for (auto const& row : rows)
    {
        BOOST_TEST_CONTEXT("revision=" << row.revision)
        {
            assertRevisionConfigMatches(revisionConfigFromRevision(row.revision), row.expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(gated_field_count_is_six)
{
    BOOST_CHECK_EQUAL(revisionConfigGatedFieldCount(), 6U);
}

BOOST_AUTO_TEST_CASE(apply_fisco_feature_gates_masks_only_a_class)
{
    using Flag = ledger::Features::Flag;
    // All flags OFF: every A-class field masked to false, B-class untouched.
    {
        auto cfg = revisionConfigFromRevision(EVMC_OSAKA);
        ledger::Features features;
        bcos::chain_policy::applyFiscoFeatureGates(cfg, features);
        BOOST_CHECK(!cfg.warm_access);
        BOOST_CHECK(!cfg.eip2537);
        BOOST_CHECK(!cfg.eip7212);
        BOOST_CHECK(!cfg.eip7623);
        BOOST_CHECK(!cfg.eip7823);
        BOOST_CHECK(!cfg.eip7702);
        // B-class survive.
        BOOST_CHECK(cfg.eip1153);
        BOOST_CHECK(cfg.eip4844);
        BOOST_CHECK(cfg.eip6780);
        BOOST_CHECK(cfg.eip1559);
        BOOST_CHECK(cfg.eip3651);
    }
    // Prague flag ON: prague-group A-class survive, osaka-group still masked.
    {
        auto cfg = revisionConfigFromRevision(EVMC_OSAKA);
        ledger::Features features;
        features.set(Flag::feature_evm_eip2929);
        features.set(Flag::feature_evm_prague);
        bcos::chain_policy::applyFiscoFeatureGates(cfg, features);
        BOOST_CHECK(cfg.warm_access);
        BOOST_CHECK(cfg.eip2537);
        BOOST_CHECK(cfg.eip7623);
        BOOST_CHECK(cfg.eip7702);
        BOOST_CHECK(!cfg.eip7212);
        BOOST_CHECK(!cfg.eip7823);
    }
}

BOOST_AUTO_TEST_CASE(eth_policy_full_fork_snapshots)
{
    EthPolicy policy;
    struct Row
    {
        int64_t block;
        ExpectedRevisionConfig expected;
    };
    std::vector<Row> const rows = {
        {15'537'394, {.revision = EVMC_PARIS, .warm_access = true, .eip1559 = true}},
        {17'034'870,
            {.revision = EVMC_SHANGHAI, .warm_access = true, .eip1559 = true, .eip3651 = true}},
        {19'426'587, {.revision = EVMC_CANCUN,
                         .warm_access = true,
                         .eip1153 = true,
                         .eip4844 = true,
                         .eip5656 = true,
                         .eip6780 = true,
                         .eip1559 = true,
                         .eip3651 = true}},
        {22'000'000, {.revision = EVMC_PRAGUE,
                         .warm_access = true,
                         .eip2537 = true,
                         .eip7623 = true,
                         .eip1153 = true,
                         .eip4844 = true,
                         .eip5656 = true,
                         .eip6780 = true,
                         .eip1559 = true,
                         .eip3651 = true,
                         .eip7702 = true,
                         .calldata_floor_per_token = 10}},
        {25'000'000, {.revision = EVMC_OSAKA,
                         .warm_access = true,
                         .eip2537 = true,
                         .eip7212 = true,
                         .eip7623 = true,
                         .eip7823 = true,
                         .eip1153 = true,
                         .eip4844 = true,
                         .eip5656 = true,
                         .eip6780 = true,
                         .eip1559 = true,
                         .eip3651 = true,
                         .eip7702 = true,
                         .calldata_floor_per_token = 10}},
    };
    for (auto const& row : rows)
    {
        BOOST_TEST_CONTEXT("block=" << row.block)
        {
            auto header =
                makeHeader(row.block, static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));
            assertRevisionConfigMatches(policy.computeRevisionConfig(header), row.expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(fisco_policy_feature_gate_snapshots)
{
    using Flag = ledger::Features::Flag;
    struct Row
    {
        std::function<void(ledger::Features&)> setup;
        ExpectedRevisionConfig expected;
    };
    std::vector<Row> const rows = {
        {[&](ledger::Features& f) {
             f.set(Flag::feature_evm_cancun);
             f.set(Flag::feature_evm_eip2929);
         },
            {.revision = EVMC_CANCUN,
                .warm_access = true,
                .eip1153 = true,
                .eip4844 = true,
                .eip5656 = true,
                .eip6780 = true,
                .eip1559 = true}},
        {[&](ledger::Features& f) {
             f.set(Flag::feature_evm_cancun);
             f.set(Flag::feature_evm_prague);
             f.set(Flag::feature_evm_eip2929);
         },
            {.revision = EVMC_PRAGUE,
                .warm_access = true,
                .eip2537 = true,
                .eip7623 = true,
                .eip1153 = true,
                .eip4844 = true,
                .eip5656 = true,
                .eip6780 = true,
                .eip1559 = true,
                .eip7702 = true,
                .calldata_floor_per_token = 10}},
        {[&](ledger::Features& f) {
             f.set(Flag::feature_evm_cancun);
             f.set(Flag::feature_evm_prague);
             f.set(Flag::feature_evm_osaka);
             f.set(Flag::feature_evm_eip2929);
         },
            {.revision = EVMC_OSAKA,
                .warm_access = true,
                .eip2537 = true,
                .eip7212 = true,
                .eip7623 = true,
                .eip7823 = true,
                .eip1153 = true,
                .eip4844 = true,
                .eip5656 = true,
                .eip6780 = true,
                .eip1559 = true,
                .eip7702 = true,
                .calldata_floor_per_token = 10}},
        {[&](ledger::Features& /*unused*/) {}, {.revision = EVMC_CANCUN,
                                                   .warm_access = false,
                                                   .eip1153 = true,
                                                   .eip4844 = true,
                                                   .eip5656 = true,
                                                   .eip6780 = true,
                                                   .eip1559 = true}},
    };
    for (auto const& row : rows)
    {
        ledger::Features features;
        row.setup(features);
        bcos::chain_policy::FiscoPolicy policy(features, false, false);
        auto header = makeHeader(1, static_cast<uint32_t>(protocol::BlockVersion::V3_2_VERSION));
        assertRevisionConfigMatches(policy.computeRevisionConfig(header).eth(), row.expected);
    }
}

BOOST_AUTO_TEST_CASE(isthmus_helper_sparse_profile_all_fields)
{
    assertIsthmusHelperProfile(makeIsthmusRevisionConfig());
}

BOOST_AUTO_TEST_SUITE_END()
