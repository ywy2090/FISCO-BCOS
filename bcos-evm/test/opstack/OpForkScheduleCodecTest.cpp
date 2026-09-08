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
}

BOOST_AUTO_TEST_CASE(RejectsKarstUntilK3)
{
    const auto isKarstLocked = [](InvalidOpForkSchedule const& e) {
        auto w = std::string_view{e.what()};
        return w.find("karst") != std::string_view::npos ||
               w.find("unknown") != std::string_view::npos;
    };
    BOOST_CHECK_EXCEPTION(parseOpForkSchedule("0:isthmus,1764691201:jovian,1783526401:karst"),
        InvalidOpForkSchedule, isKarstLocked);
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:jovian,1781712001:karst"), InvalidOpForkSchedule, isKarstLocked);
    BOOST_CHECK_EXCEPTION(parseOpForkSchedule("0:karst"), InvalidOpForkSchedule, isKarstLocked);
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
            return std::string_view{e.what()}.find("unknown") != std::string_view::npos;
        });
    BOOST_CHECK_THROW(parseOpForkSchedule(""), InvalidOpForkSchedule);
    BOOST_CHECK_THROW(parseOpForkSchedule("0:jovian,1:isthmus"), InvalidOpForkSchedule);
}

BOOST_AUTO_TEST_SUITE_END()
