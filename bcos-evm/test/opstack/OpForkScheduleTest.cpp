#include "TestPrinters.h"
#include "support/KarstScheduleFixtures.h"
#include "support/OpForkFlagsCompat.h"
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPrecompiles.h>
#include <bcos-framework/ledger/OpForkScheduleCodec.h>
#include <boost/test/unit_test.hpp>
#include <cstddef>
#include <string>
#include <utility>

using namespace bcos::evm::opstack;
using bcos::ledger::InvalidOpForkSchedule;

BOOST_AUTO_TEST_SUITE(OpForkScheduleSuite)

BOOST_AUTO_TEST_CASE(IsthmusMapsToPrague)
{
    const auto& cfg = isthmusConfig();
    BOOST_CHECK_EQUAL(cfg.fork, OpFork::Isthmus);
    BOOST_CHECK_EQUAL(cfg.rev, EVMC_PRAGUE);
    BOOST_CHECK(cfg.disable_prague_requests);
    BOOST_CHECK(cfg.has_operator_fee);
}

BOOST_AUTO_TEST_CASE(JovianAndKarstConfigs)
{
    const auto& j = jovianConfig();
    BOOST_CHECK_EQUAL(j.fork, OpFork::Jovian);
    BOOST_CHECK_EQUAL(j.rev, EVMC_PRAGUE);
    BOOST_CHECK(j.has_operator_fee);
    BOOST_CHECK(j.has_jovian_operator_formula);
    BOOST_CHECK(j.has_da_footprint);
    BOOST_CHECK(j.disable_prague_requests);
    BOOST_CHECK((j.precompiles) != nullptr);

    const auto& k = karstConfig();
    BOOST_CHECK_EQUAL(k.fork, OpFork::Karst);
    BOOST_CHECK_EQUAL(k.rev, EVMC_OSAKA);
    BOOST_CHECK_EQUAL(k.has_operator_fee, j.has_operator_fee);
    BOOST_CHECK_EQUAL(k.has_jovian_operator_formula, j.has_jovian_operator_formula);
    BOOST_CHECK_EQUAL(k.has_da_footprint, j.has_da_footprint);
}

BOOST_AUTO_TEST_CASE(IsthmusDisablesJovianFlags)
{
    const auto& i = isthmusConfig();
    BOOST_CHECK(!(i.has_jovian_operator_formula));
    BOOST_CHECK(!(i.has_da_footprint));
}

// Feature-flag fork selection (feature_op_jovian replaces the former timestamp thresholds):
// OFF → Isthmus baseline, ON → Jovian semantics.
BOOST_AUTO_TEST_CASE(ConfigAtSelectsForkByFeatureFlag)
{
    // Value copies, not references: configAt returns a reference to a static config, but the
    // OpForkFlags{...} argument is a prvalue temporary — GCC-14 -Wdangling-reference flags the
    // reference binding as potentially dangling (false positive; the returned ref never aliases
    // the flags argument). Copy the ~32B config instead.
    const auto ist = configAt(OpForkFlags{.jovianActive = false});
    BOOST_CHECK_EQUAL(ist.fork, OpFork::Isthmus);
    BOOST_CHECK(!ist.has_jovian_operator_formula);
    BOOST_CHECK(!ist.has_da_footprint);

    const auto jov = configAt(OpForkFlags{.jovianActive = true});
    BOOST_CHECK_EQUAL(jov.fork, OpFork::Jovian);
    BOOST_CHECK(jov.has_jovian_operator_formula);
    BOOST_CHECK(jov.has_da_footprint);
}

// 覆盖剩余字段 has_ecotone_l1_formula（Ecotone 用 calldataGas、Fjord+ 用 FastLZ）。
// 测试专用 configAt(OpForkFlags) 只有 Isthmus/Jovian 两分支，选不出 Karst。
// 生产路径是 timestamp OpForkSchedule::configAt，Karst 时间戳返回 karstConfig()/Osaka。
BOOST_AUTO_TEST_CASE(EcotoneFormulaFlagAndFlagsWrapperDoesNotSelectKarst)
{
    BOOST_CHECK(ecotoneConfig().has_ecotone_l1_formula);
    BOOST_CHECK(!(fjordConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(graniteConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(holoceneConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(isthmusConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(jovianConfig().has_ecotone_l1_formula));

    BOOST_CHECK_EQUAL(&configAt(OpForkFlags{.jovianActive = false}), &isthmusConfig());
    BOOST_CHECK_EQUAL(&configAt(OpForkFlags{.jovianActive = true}), &jovianConfig());
}

BOOST_AUTO_TEST_CASE(L1FeeModelPinnedOnExistingConfigs)
{
    BOOST_CHECK(ecotoneConfig().l1_fee_model == L1FeeModel::Ecotone);
    BOOST_CHECK(fjordConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(graniteConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(holoceneConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(isthmusConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(jovianConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(karstConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(ecotoneConfig().has_ecotone_l1_formula);
    BOOST_CHECK(!fjordConfig().has_ecotone_l1_formula);
}

BOOST_AUTO_TEST_CASE(RegolithCanyonConfigsAndTimestampSelect)
{
    BOOST_CHECK_EQUAL(regolithConfig().rev, EVMC_LONDON);
    BOOST_CHECK(regolithConfig().l1_fee_model == L1FeeModel::Bedrock);
    BOOST_CHECK(!regolithConfig().has_operator_fee);
    BOOST_CHECK(!regolithConfig().has_da_footprint);
    BOOST_CHECK_EQUAL(canyonConfig().rev, EVMC_SHANGHAI);
    BOOST_CHECK(canyonConfig().l1_fee_model == L1FeeModel::Bedrock);

    OpForkSchedule sched(
        {
            {OpFork::Regolith, 0},
            {OpFork::Canyon, 100},
            {OpFork::Ecotone, 200},
            {OpFork::Fjord, 300},
            {OpFork::Holocene, 400},
            {OpFork::Isthmus, 500},
        },
        OpForkSchedule::TestBypass{});

    BOOST_CHECK_EQUAL(sched.forkAt(0), OpFork::Regolith);
    BOOST_CHECK_EQUAL(sched.forkAt(99), OpFork::Regolith);
    BOOST_CHECK_EQUAL(sched.forkAt(100), OpFork::Canyon);
    BOOST_CHECK_EQUAL(sched.forkAt(200), OpFork::Ecotone);
    BOOST_CHECK_EQUAL(&sched.configAt(0), &regolithConfig());
    BOOST_CHECK_EQUAL(&sched.configAt(150), &canyonConfig());
    BOOST_CHECK_EQUAL(&sched.configAt(200), &ecotoneConfig());
    BOOST_CHECK(sched.jovianAndLaterActivations().empty());
}

// The codec accepts any EL fork as a baseline, so production parse must map the
// name instead of rejecting it.
BOOST_AUTO_TEST_CASE(ParseAcceptsRegolithBaseline)
{
    auto s = OpForkSchedule::parse("0:regolith");
    BOOST_CHECK_EQUAL(s.forkAt(0), OpFork::Regolith);
    BOOST_CHECK_EQUAL(&s.configAt(0), &regolithConfig());
}

// OpFork's enumerator order is the codec table's index order; inserting a fork
// in the middle of either one without the other fails here.
BOOST_AUTO_TEST_CASE(ForkNameEnumRoundTripsAllNine)
{
    using bcos::ledger::detail::c_opForkNames;
    // The codec table infers its own size, so the protocol cardinality is pinned
    // here: exactly the nine op-geth EL forks, with no delta entry.
    BOOST_CHECK_EQUAL(c_opForkNames.size(), 9u);
    for (std::size_t i = 0; i < c_opForkNames.size(); ++i)
    {
        auto s = OpForkSchedule::parse("0:" + std::string(c_opForkNames[i]));
        BOOST_CHECK_EQUAL(s.forkAt(0), static_cast<OpFork>(i));
    }
    BOOST_CHECK_EQUAL(static_cast<std::size_t>(OpFork::Karst), c_opForkNames.size() - 1);
}

BOOST_AUTO_TEST_CASE(PreIsthmusConfigsPinned)
{
    for (const auto* cfg : {&ecotoneConfig(), &fjordConfig(), &graniteConfig(), &holoceneConfig()})
    {
        BOOST_CHECK_EQUAL(cfg->rev, EVMC_CANCUN);
        BOOST_CHECK(cfg->disable_prague_requests);
        BOOST_CHECK(!(cfg->has_operator_fee));
        BOOST_CHECK(!(cfg->has_jovian_operator_formula));
        BOOST_CHECK(!(cfg->has_da_footprint));
    }
    BOOST_CHECK_EQUAL(ecotoneConfig().precompiles, nullptr);  // Ecotone 早于 Fjord/Granite/Holocene
                                                              // 表
    for (const auto* cfg : {&fjordConfig(), &graniteConfig(), &holoceneConfig()})
    {
        BOOST_CHECK((cfg->precompiles) != nullptr);  // 明细由 FjordOnward* 用例覆盖
    }
    BOOST_CHECK_EQUAL(ecotoneConfig().fork, OpFork::Ecotone);
    BOOST_CHECK_EQUAL(fjordConfig().fork, OpFork::Fjord);
    BOOST_CHECK_EQUAL(graniteConfig().fork, OpFork::Granite);
    BOOST_CHECK_EQUAL(holoceneConfig().fork, OpFork::Holocene);
    BOOST_CHECK(ecotoneConfig().has_ecotone_l1_formula);
    BOOST_CHECK(!(fjordConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(graniteConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(holoceneConfig().has_ecotone_l1_formula));
}

BOOST_AUTO_TEST_CASE(IsthmusPlusDisableEcotoneL1Formula)
{
    BOOST_CHECK(!(isthmusConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(jovianConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(karstConfig().has_ecotone_l1_formula));
}

// D-15：op-geth 自 Fjord 起 0x100 P256VERIFY 活跃（contracts.go:193，gas 3450 params:183）；
// D-11：bn256Pairing 112687 上限自 Granite 起（params:172，Holocene 沿用）
BOOST_AUTO_TEST_CASE(FjordOnwardCarryP256VerifyAndGraniteCapsBn256)
{
    BOOST_CHECK_EQUAL(ecotoneConfig().precompiles, nullptr);  // Ecotone 早于两者

    for (const auto* cfg : {&fjordConfig(), &graniteConfig(), &holoceneConfig()})
    {
        BOOST_REQUIRE((cfg->precompiles) != nullptr);
        const auto* p256 = cfg->precompiles->find(evmc::address{0x100});
        BOOST_REQUIRE((p256) != nullptr);
        BOOST_CHECK_EQUAL(p256->gas_cost_override, 3450);
    }
    BOOST_CHECK(!(fjordConfig().precompiles->contains(evmc::address{0x08})));  // cap 是 Granite 的
    for (const auto* cfg : {&graniteConfig(), &holoceneConfig()})
    {
        const auto* bn256 = cfg->precompiles->find(evmc::address{0x08});
        BOOST_REQUIRE((bn256) != nullptr);
        BOOST_CHECK_EQUAL(bn256->max_input_size, 112687u);
        BOOST_CHECK_EQUAL(bn256->gas_cost_override, -1);
        BOOST_CHECK(!(cfg->precompiles->contains(evmc::address{0x0c})));  // BLS 是 PRAGUE 的
    }
}

BOOST_AUTO_TEST_CASE(ConfigAtTimestampSelectsIsthmusThenJovian)
{
    auto schedule = OpForkSchedule::parse("0:isthmus,1764691201:jovian");
    BOOST_CHECK_EQUAL(schedule.forkAt(0), OpFork::Isthmus);
    BOOST_CHECK_EQUAL(schedule.forkAt(1764691200), OpFork::Isthmus);
    BOOST_CHECK_EQUAL(schedule.forkAt(1764691201), OpFork::Jovian);
    BOOST_CHECK_EQUAL(schedule.configAt(1764691200).rev, EVMC_PRAGUE);
    BOOST_CHECK(!schedule.configAt(1764691200).has_da_footprint);
    BOOST_CHECK(schedule.configAt(1764691201).has_da_footprint);
    BOOST_CHECK_THROW(OpForkSchedule::parse("0:isthmus,1:karst"), InvalidOpForkSchedule);
}

BOOST_AUTO_TEST_CASE(LegacyFlagsStillSelectIsthmusOrJovian)
{
    BOOST_CHECK_EQUAL(OpForkSchedule::legacy(false).forkAt(0), OpFork::Isthmus);
    BOOST_CHECK_EQUAL(OpForkSchedule::legacy(true).forkAt(0), OpFork::Jovian);
}

BOOST_AUTO_TEST_CASE(EmptyScheduleRejected)
{
    BOOST_CHECK_THROW(OpForkSchedule::parse(""), InvalidOpForkSchedule);
    BOOST_CHECK_THROW(OpForkSchedule{{}}, InvalidOpForkSchedule);

    auto schedule = OpForkSchedule::legacy(false);
    auto kept = std::move(schedule);
    BOOST_CHECK_EQUAL(kept.forkAt(0), OpFork::Isthmus);
    BOOST_CHECK_THROW(static_cast<void>(schedule.forkAt(0)), InvalidOpForkSchedule);
    BOOST_CHECK_THROW(static_cast<void>(schedule.configAt(0)), InvalidOpForkSchedule);
}

BOOST_AUTO_TEST_CASE(ConfigAtTimestampMatchesForkAndStaticConfigs)
{
    auto schedule = OpForkSchedule::parse("0:isthmus,1764691201:jovian");
    BOOST_CHECK_EQUAL(schedule.configAt(1764691200).fork, schedule.forkAt(1764691200));
    BOOST_CHECK_EQUAL(schedule.configAt(1764691201).fork, schedule.forkAt(1764691201));
    BOOST_CHECK_EQUAL(&schedule.configAt(1764691200), &isthmusConfig());
    BOOST_CHECK_EQUAL(&schedule.configAt(1764691201), &jovianConfig());
}

BOOST_AUTO_TEST_CASE(KarstConfigIsOsakaNotJovianAlias)
{
    const auto& k = karstConfig();
    BOOST_CHECK_EQUAL(k.fork, OpFork::Karst);
    BOOST_CHECK_EQUAL(k.rev, EVMC_OSAKA);
    BOOST_CHECK(k.deposit_exempt_from_max_tx_gas);
    BOOST_CHECK(k.precompiles != jovianConfig().precompiles);
}

BOOST_AUTO_TEST_CASE(KarstImpliesOsakaConfig)
{
    auto s = OpForkSchedule::parse("0:jovian,1783526401:karst");
    BOOST_CHECK_EQUAL(s.configAt(1783526401).rev, EVMC_OSAKA);
    BOOST_CHECK(s.configAt(1783526401).deposit_exempt_from_max_tx_gas);
}

BOOST_AUTO_TEST_CASE(JovianAndLaterActivationsAreNamedForks)
{
    auto isthmusOnly = OpForkSchedule::parse("0:isthmus");
    BOOST_CHECK(isthmusOnly.jovianAndLaterActivations().empty());

    auto withJovian = OpForkSchedule::parse("0:isthmus,1764691201:jovian");
    auto const jovianSlice = withJovian.jovianAndLaterActivations();
    BOOST_REQUIRE_EQUAL(jovianSlice.size(), 1);
    BOOST_CHECK(jovianSlice[0].fork == OpFork::Jovian);

    auto withKarst = OpForkSchedule::parse("0:jovian,1783526401:karst");
    auto const karstSlice = withKarst.jovianAndLaterActivations();
    BOOST_REQUIRE_EQUAL(karstSlice.size(), 2);
    BOOST_CHECK(karstSlice[0].fork == OpFork::Jovian);
    BOOST_CHECK(karstSlice[1].fork == OpFork::Karst);

    auto preIsthmus =
        OpForkSchedule{{{OpFork::Ecotone, 0}, {OpFork::Jovian, 10}}, OpForkSchedule::TestBypass{}};
    auto const named = preIsthmus.jovianAndLaterActivations();
    BOOST_REQUIRE_EQUAL(named.size(), 1);
    BOOST_CHECK(named[0].fork == OpFork::Jovian);
}

BOOST_AUTO_TEST_CASE(TestBypassScheduleCanNameKarst)
{
    auto s =
        OpForkSchedule{{{OpFork::Jovian, 0}, {OpFork::Karst, 100}}, OpForkSchedule::TestBypass{}};
    BOOST_CHECK_EQUAL(s.forkAt(99), OpFork::Jovian);
    BOOST_CHECK_EQUAL(s.forkAt(100), OpFork::Karst);
    BOOST_CHECK_EQUAL(s.configAt(100).rev, EVMC_OSAKA);

    const auto only = karstOnly();
    BOOST_CHECK_EQUAL(only.forkAt(2), OpFork::Karst);
    BOOST_CHECK_EQUAL(only.configAt(2).rev, EVMC_OSAKA);
}

BOOST_AUTO_TEST_SUITE_END()
