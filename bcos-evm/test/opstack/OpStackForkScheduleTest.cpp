#define BOOST_TEST_MODULE OpStackForkScheduleTest

#include "bcos-evm/opstack/OpStackForkSchedule.h"
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

}  // namespace bcos::evm::test
