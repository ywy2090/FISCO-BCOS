#include <bcos-framework/ledger/OpForkScheduleCodec.h>
#include <boost/test/unit_test.hpp>
#include <string>
#include <string_view>

using bcos::ledger::InvalidOpForkSchedule;
using bcos::ledger::parseOpForkSchedule;

BOOST_AUTO_TEST_SUITE(OpForkScheduleCodecSuite)

BOOST_AUTO_TEST_CASE(AcceptsIsthmusJovianOnly)
{
    auto acts = parseOpForkSchedule("0:isthmus,1764691201:jovian");
    BOOST_REQUIRE_EQUAL(acts.size(), 2u);
    BOOST_CHECK_EQUAL(acts[0].forkName, "isthmus");
    BOOST_CHECK_EQUAL(acts[0].timestamp, 0u);
    BOOST_CHECK_EQUAL(acts[1].forkName, "jovian");
    BOOST_CHECK_EQUAL(acts[1].timestamp, 1764691201u);
}

BOOST_AUTO_TEST_CASE(AcceptsKarstAfterJovian)
{
    auto acts = parseOpForkSchedule("0:isthmus,1764691201:jovian,1783526401:karst");
    BOOST_REQUIRE_EQUAL(acts.size(), 3u);
    BOOST_CHECK_EQUAL(acts[2].forkName, "karst");
}

BOOST_AUTO_TEST_CASE(AcceptsAllNineElForkBaselines)
{
    for (auto const* name : {"regolith", "canyon", "ecotone", "fjord", "granite", "holocene",
             "isthmus", "jovian", "karst"})
    {
        auto acts = parseOpForkSchedule(std::string("0:") + name);
        BOOST_REQUIRE_EQUAL(acts.size(), 1u);
        BOOST_CHECK_EQUAL(acts[0].forkName, name);
    }
}

BOOST_AUTO_TEST_CASE(AcceptsFullOfficialChain)
{
    auto acts = parseOpForkSchedule(
        "0:regolith,1000:canyon,2000:ecotone,3000:fjord,4000:granite,"
        "5000:holocene,6000:isthmus,7000:jovian,8000:karst");
    BOOST_REQUIRE_EQUAL(acts.size(), 9u);
    BOOST_CHECK_EQUAL(acts[8].forkName, "karst");
    BOOST_CHECK_EQUAL(acts[8].timestamp, 8000u);
}

BOOST_AUTO_TEST_CASE(NormalizesCaseAndWhitespace)
{
    auto acts = parseOpForkSchedule("0: Regolith ,1000:Canyon");
    BOOST_REQUIRE_EQUAL(acts.size(), 2u);
    BOOST_CHECK_EQUAL(acts[0].forkName, "regolith");
    BOOST_CHECK_EQUAL(acts[1].forkName, "canyon");
}

// Baseline is any known EL fork, so both of these are now legal; the gap case is
// still rejected, just by the general contiguity rule rather than a Karst/Jovian
// special case.
BOOST_AUTO_TEST_CASE(RejectsSkippedFork)
{
    const auto isGap = [](InvalidOpForkSchedule const& e) {
        return std::string_view{e.what()}.find("forks out of protocol order") !=
               std::string_view::npos;
    };
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:isthmus,1783526401:karst"), InvalidOpForkSchedule, isGap);
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:regolith,100:ecotone"), InvalidOpForkSchedule, isGap);
}

BOOST_AUTO_TEST_CASE(RejectsTimestampOverflow)
{
    const auto isOverflow = [](InvalidOpForkSchedule const& e) {
        return std::string_view{e.what()}.find("timestamp overflow") != std::string_view::npos;
    };
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("18446744073709551617:isthmus"), InvalidOpForkSchedule, isOverflow);
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("25000000000000000000:isthmus"), InvalidOpForkSchedule, isOverflow);
}

BOOST_AUTO_TEST_CASE(RejectsTooManyActivations)
{
    const auto isTooMany = [](InvalidOpForkSchedule const& e) {
        return std::string_view{e.what()}.find("too many activations") != std::string_view::npos;
    };
    // cap 16：第 17 条在 push 前抛。
    std::string canonical = "0:regolith";
    for (uint64_t i = 1; i <= 16; ++i)
    {
        canonical += "," + std::to_string(i) + ":regolith";
    }
    BOOST_CHECK_EXCEPTION(parseOpForkSchedule(canonical), InvalidOpForkSchedule, isTooMany);
}

BOOST_AUTO_TEST_CASE(RejectsEmptyMissingBaselineAndOrder)
{
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("1:isthmus"), InvalidOpForkSchedule, [](auto const& e) {
            return std::string_view{e.what()}.find("missing timestamp-0 baseline") !=
                   std::string_view::npos;
        });
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:notafork"), InvalidOpForkSchedule, [](auto const& e) {
            return std::string_view{e.what()}.find("invalid baseline") != std::string_view::npos;
        });
    BOOST_CHECK_EXCEPTION(parseOpForkSchedule(""), InvalidOpForkSchedule, [](auto const& e) {
        return std::string_view{e.what()}.find("empty schedule") != std::string_view::npos;
    });
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:jovian,1:isthmus"), InvalidOpForkSchedule, [](auto const& e) {
            return std::string_view{e.what()}.find("forks out of protocol order") !=
                   std::string_view::npos;
        });
    // An unknown name past the baseline keeps the non-baseline branch covered.
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:isthmus,1:notafork"), InvalidOpForkSchedule, [](auto const& e) {
            return std::string_view{e.what()}.find("unknown") != std::string_view::npos;
        });
}

BOOST_AUTO_TEST_CASE(RejectsDuplicateTimestamp)
{
    const auto isDup = [](InvalidOpForkSchedule const& e) {
        return std::string_view{e.what()}.find("duplicate timestamp") != std::string_view::npos;
    };
    // 第 3 条 ts 与前一条相同：先于连续检查报错。
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:isthmus,100:jovian,100:karst"), InvalidOpForkSchedule, isDup);
}

BOOST_AUTO_TEST_CASE(RejectsTrailingComma)
{
    const auto isTrailing = [](InvalidOpForkSchedule const& e) {
        return std::string_view{e.what()}.find("trailing comma") != std::string_view::npos;
    };
    BOOST_CHECK_EXCEPTION(parseOpForkSchedule("0:isthmus,"), InvalidOpForkSchedule, isTrailing);
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:isthmus,1764691201:jovian,"), InvalidOpForkSchedule, isTrailing);
}

BOOST_AUTO_TEST_SUITE_END()
