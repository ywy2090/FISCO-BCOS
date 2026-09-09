#include <bcos-framework/ledger/OpForkScheduleCodec.h>
#include <boost/test/unit_test.hpp>
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

BOOST_AUTO_TEST_CASE(RejectsKarstWithoutJovian)
{
    BOOST_CHECK_EXCEPTION(parseOpForkSchedule("0:isthmus,1783526401:karst"), InvalidOpForkSchedule,
        [](InvalidOpForkSchedule const& e) {
            return std::string_view{e.what()}.find("Jovian activation is required before Karst") !=
                   std::string_view::npos;
        });
    // Baseline cannot be karst (isAllowedBaseline stays isthmus|jovian).
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:karst"), InvalidOpForkSchedule, [](InvalidOpForkSchedule const& e) {
            return std::string_view{e.what()}.find("invalid baseline") != std::string_view::npos;
        });
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
    // 源 codec 先 count 再 validate；第 9 段在 push 前抛 "too many activations"。
    // 仅 8 段会在 validate 阶段抛 "duplicate fork" —— 勿用 8 段测 too-many。
    BOOST_CHECK_EXCEPTION(parseOpForkSchedule("0:isthmus,1:jovian,2:isthmus,3:jovian,"
                                              "4:isthmus,5:jovian,6:isthmus,7:jovian,8:isthmus"),
        InvalidOpForkSchedule, isTooMany);
}

BOOST_AUTO_TEST_CASE(RejectsEmptyMissingBaselineAndOrder)
{
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("1:isthmus"), InvalidOpForkSchedule, [](auto const& e) {
            return std::string_view{e.what()}.find("missing timestamp-0 baseline") !=
                   std::string_view::npos;
        });
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:ecotone"), InvalidOpForkSchedule, [](auto const& e) {
            auto w = std::string_view{e.what()};
            return w.find("invalid baseline") != std::string_view::npos ||
                   w.find("unknown") != std::string_view::npos;
        });
    BOOST_CHECK_EXCEPTION(parseOpForkSchedule(""), InvalidOpForkSchedule, [](auto const& e) {
        return std::string_view{e.what()}.find("empty schedule") != std::string_view::npos;
    });
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:jovian,1:isthmus"), InvalidOpForkSchedule, [](auto const& e) {
            return std::string_view{e.what()}.find("forks out of protocol order") !=
                   std::string_view::npos;
        });
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
