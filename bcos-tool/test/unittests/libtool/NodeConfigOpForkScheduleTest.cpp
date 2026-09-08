/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "ExceptionCheck.h"
#include "NodeConfigLoaderProbe.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigOpForkScheduleTest)

BOOST_AUTO_TEST_CASE(parsesNormalizedCanonical)
{
    LoaderProbe probe;
    probe.loadOpForkSchedule(
        fromIni("[op_fork_schedule]\n"
                "canonical=0:isthmus,1764691201:jovian\n"));
    BOOST_REQUIRE(probe.genesisConfig().m_opstackForkSchedule.has_value());
    BOOST_CHECK_EQUAL(*probe.genesisConfig().m_opstackForkSchedule, "0:isthmus,1764691201:jovian");
}

BOOST_AUTO_TEST_CASE(rejectsKarst)
{
    LoaderProbe probe;
    BOOST_CHECK_EXCEPTION(probe.loadOpForkSchedule(fromIni("[op_fork_schedule]\n"
                                                           "canonical=0:jovian,1:karst\n")),
        InvalidConfig, [](auto const& e) { return errinfoContains(e, "op_fork_schedule"); });
}

BOOST_AUTO_TEST_CASE(missingSectionLeavesScheduleUnset)
{
    LoaderProbe probe;
    BOOST_CHECK_NO_THROW(probe.loadOpForkSchedule(fromIni("[chain]\nchain_id=1\n")));
    BOOST_CHECK(!probe.genesisConfig().m_opstackForkSchedule.has_value());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
