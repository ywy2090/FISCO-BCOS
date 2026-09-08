/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file OpEngineKarstProfileTest.cpp
 * @brief getPayload profile is keyed on payload/attrs timestamp (A9/A13).
 *
 * Karst (attrs.timestamp = 1000s as 1000_000 ms) must reject getPayload V4 with
 * UnsupportedFork (-38005) and accept V5. Jovian (999_000 ms) is the inverse.
 */
#include "support/OpEngineKarstTestHarness.h"

#include <boost/test/unit_test.hpp>
#include <algorithm>

using namespace op_engine_parity_test;

BOOST_AUTO_TEST_SUITE(OpEngineKarstProfileSuite)

BOOST_AUTO_TEST_CASE(KarstPayloadTimestampRejectsGetPayloadV4)
{
    BOOST_CHECK_EQUAL(
        bcos::engine::unixSecondsFromInternalMillis(c_karstPayloadTimestampMs), 1000U);
    KarstProfilePair fixture;
    auto const payloadId =
        buildPayloadAt(fixture.pair, c_karstPayloadTimestampMs, c_jovianPayloadTimestampMs);
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture.pair.service.getPayload(payloadId, 4)),
        bcos::engine::UnsupportedFork);
}

BOOST_AUTO_TEST_CASE(KarstPayloadTimestampAcceptsGetPayloadV5)
{
    KarstProfilePair fixture;
    auto const payloadId =
        buildPayloadAt(fixture.pair, c_karstPayloadTimestampMs, c_jovianPayloadTimestampMs);
    auto result = bcos::task::syncWait(fixture.pair.service.getPayload(payloadId, 5));
    BOOST_REQUIRE(result);
    BOOST_REQUIRE(result->executionRequests.has_value());
    BOOST_CHECK(result->executionRequests->empty());
}

BOOST_AUTO_TEST_CASE(JovianPayloadTimestampRejectsGetPayloadV5)
{
    KarstProfilePair fixture;
    auto const payloadId =
        buildPayloadAt(fixture.pair, c_jovianPayloadTimestampMs, /*parent*/ 998'000);
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture.pair.service.getPayload(payloadId, 5)),
        bcos::engine::UnsupportedFork);
}

BOOST_AUTO_TEST_CASE(JovianPayloadTimestampAcceptsGetPayloadV4)
{
    // #5550 regression: Jovian payload timestamp still serves getPayload V4.
    KarstProfilePair fixture;
    auto const payloadId =
        buildPayloadAt(fixture.pair, c_jovianPayloadTimestampMs, /*parent*/ 998'000);
    auto result = bcos::task::syncWait(fixture.pair.service.getPayload(payloadId, 4));
    BOOST_REQUIRE(result);
}

BOOST_AUTO_TEST_CASE(ActivationBlockFcuUsesAttrTimestampNotHead)
{
    // Head is still Jovian (999_000 ms); attrs.timestamp is Karst (1000_000 ms).
    // Profile must follow the payload timestamp, never the head.
    KarstProfilePair fixture;
    auto const payloadId =
        buildPayloadAt(fixture.pair, c_karstPayloadTimestampMs, c_jovianPayloadTimestampMs);
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture.pair.service.getPayload(payloadId, 4)),
        bcos::engine::UnsupportedFork);
    auto v5 = bcos::task::syncWait(fixture.pair.service.getPayload(payloadId, 5));
    BOOST_REQUIRE(v5);
}

BOOST_AUTO_TEST_CASE(CapabilitiesAlwaysAdvertiseV4AndV5)
{
    KarstProfilePair fixture;
    auto caps = bcos::task::syncWait(fixture.pair.service.exchangeCapabilities({}));
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_getPayloadV4") != caps.end());
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_getPayloadV5") != caps.end());
}

BOOST_AUTO_TEST_SUITE_END()
