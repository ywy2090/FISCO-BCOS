#define BOOST_TEST_MODULE OpStackForkScheduleTest

#include "bcos-evm/opstack/policy/OpStackForkSchedule.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{

BOOST_AUTO_TEST_CASE(preset_activates_fjord_and_isthmus_at_block_one)
{
    auto const schedule = makeIsthmusPlusForkSchedule();
    BOOST_CHECK(isOpStackFjord(schedule, 1));
    BOOST_CHECK(isOpStackIsthmus(schedule, 1));
}

BOOST_AUTO_TEST_CASE(fork_time_zero_active_at_genesis)
{
    OpStackForkSchedule const schedule{.fjordTime = 0, .isthmusTime = 0};
    BOOST_CHECK(isOpStackFjord(schedule, 0));
    BOOST_CHECK(isOpStackIsthmus(schedule, 0));
}

BOOST_AUTO_TEST_CASE(nullopt_fork_inactive)
{
    OpStackForkSchedule const schedule{.fjordTime = std::nullopt, .isthmusTime = std::nullopt};
    BOOST_CHECK(!isOpStackFjord(schedule, 100));
    BOOST_CHECK(!isOpStackIsthmus(schedule, 100));
}

BOOST_AUTO_TEST_CASE(future_fork_inactive)
{
    OpStackForkSchedule const schedule{.fjordTime = 100, .isthmusTime = 100};
    BOOST_CHECK(!isOpStackFjord(schedule, 50));
    BOOST_CHECK(!isOpStackIsthmus(schedule, 50));
}

BOOST_AUTO_TEST_CASE(jovian_plus_preset_all_active_at_genesis)
{
    auto const schedule = makeJovianPlusForkSchedule();
    BOOST_REQUIRE(schedule.jovianTime.has_value());
    BOOST_CHECK_EQUAL(*schedule.jovianTime, 0u);
    BOOST_CHECK(isOpStackJovian(schedule, 0));
    BOOST_CHECK(isOpStackJovian(schedule, 999));
}

BOOST_AUTO_TEST_CASE(isthmus_plus_is_not_jovian)
{
    auto const schedule = makeIsthmusPlusForkSchedule();
    BOOST_CHECK(!isOpStackJovian(schedule, 0));
    BOOST_CHECK(!isOpStackJovian(schedule, 999));
}

BOOST_AUTO_TEST_CASE(jovian_future_timestamp_inactive)
{
    OpStackForkSchedule const schedule{.fjordTime = 0, .isthmusTime = 0, .jovianTime = 100};
    BOOST_CHECK(!isOpStackJovian(schedule, 99));
    BOOST_CHECK(isOpStackJovian(schedule, 100));
}

}  // namespace bcos::evm::test
