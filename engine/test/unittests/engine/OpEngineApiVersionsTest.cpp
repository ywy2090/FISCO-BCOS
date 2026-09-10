// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// S3: the Engine method number is chosen by op-node from the payload timestamp, so
// the EL only checks the pair. A mismatch is UnsupportedFork (JSON-RPC -38005); a
// match must not be rejected for being "the wrong version" — whatever the shape
// validation then says about the stub body is a separate concern.

#include "support/OpEngineKarstTestHarness.h"

#include <bcos-framework/engine/Errors.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>

using namespace op_engine_parity_test;
using bcos::engine::ApiVersion;
using bcos::engine::UnsupportedFork;

BOOST_AUTO_TEST_SUITE(OpEngineApiVersionsTest)

namespace
{
std::shared_ptr<bcos::evm::opstack::OpForkSchedule> historical()
{
    using F = bcos::evm::opstack::OpFork;
    using S = bcos::evm::opstack::OpForkSchedule;
    return std::make_shared<S>(
        S{{{F::Regolith, 0}, {F::Canyon, 100}, {F::Ecotone, 200}, {F::Holocene, 300},
              {F::Isthmus, 400}, {F::Jovian, 500}, {F::Karst, 600}},
            S::TestBypass{}});
}

bcos::engine::NewPayloadRequest stubAt(uint64_t tsSec)
{
    bcos::engine::NewPayloadRequest req;
    req.executionPayload.timestamp = tsSec * 1000;
    req.executionPayload.blockNumber = 1;
    req.executionPayload.withdrawals.emplace();
    return req;
}

void expectUnsupportedFork(
    OpEngine& service, bcos::engine::NewPayloadRequest const& req, std::uint32_t version)
{
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(service.newPayload(req, version)), UnsupportedFork,
        [](UnsupportedFork const&) { return true; });
}
}  // namespace

BOOST_AUTO_TEST_CASE(NewPayloadWrongVersionIsUnsupportedFork)
{
    OpServicePair pair(false, nullptr, nullptr, historical());
    // V4 + Canyon time
    expectUnsupportedFork(pair.service, stubAt(100), static_cast<uint32_t>(ApiVersion::V4));
    // V2 + Ecotone time
    expectUnsupportedFork(pair.service, stubAt(200), static_cast<uint32_t>(ApiVersion::V2));
}

BOOST_AUTO_TEST_CASE(NewPayloadMatchingVersionIsNotUnsupportedFork)
{
    OpServicePair pair(false, nullptr, nullptr, historical());
    // With the pair matched, today's shape validation still rejects or records the
    // stub rather than throwing. "Did not throw" is NOT a claim that a V2 stub is
    // well-formed.
    BOOST_CHECK_NO_THROW(static_cast<void>(bcos::task::syncWait(
        pair.service.newPayload(stubAt(100), static_cast<uint32_t>(ApiVersion::V2)))));
}

// With attrs the method number is part of the pair too: Regolith builds with FCU V1
// and Canyon with V2, so neither may be turned away as an unsupported fork.
BOOST_AUTO_TEST_CASE(FcuRegolithV1IsNotUnsupportedFork)
{
    OpServicePair pair(/*allowSynthesized=*/true, nullptr, nullptr, historical());
    auto attrs = makeRegolithAttrs(0);
    auto hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState fc{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    BOOST_CHECK_NO_THROW(static_cast<void>(bcos::task::syncWait(
        pair.service.updateForkchoice(fc, &attrs, static_cast<uint32_t>(ApiVersion::V1)))));
}

BOOST_AUTO_TEST_CASE(FcuCanyonV2IsNotUnsupportedFork)
{
    OpServicePair pair(/*allowSynthesized=*/true, nullptr, nullptr, historical());
    auto attrs = makeCanyonAttrs(100'000);
    auto hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState fc{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    // Only the gate is under test: a build that fails for want of a parent header
    // comes back Invalid, not an unsupported fork. Getting a payloadId is Task 9.
    BOOST_CHECK_NO_THROW(static_cast<void>(bcos::task::syncWait(
        pair.service.updateForkchoice(fc, &attrs, static_cast<uint32_t>(ApiVersion::V2)))));
}

BOOST_AUTO_TEST_CASE(FcuV2AtIsthmusIsUnsupportedFork)
{
    OpServicePair pair(true, nullptr, nullptr, historical());
    auto attrs = makeCanyonAttrs(400'000);
    auto hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState fc{hash, hash, hash};
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(pair.service.updateForkchoice(
                              fc, &attrs, static_cast<uint32_t>(ApiVersion::V2))),
        UnsupportedFork, [](UnsupportedFork const&) { return true; });
}

// The OP-specific attrs rules key on the fork's extraData layout, not on a Jovian
// boolean: pre-Holocene carries no 1559 params at all.
BOOST_AUTO_TEST_CASE(PreHoloceneAttrsRejectEip1559Params)
{
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee.reset();
    auto err =
        engine_common::op::validateOpPayloadAttributes(attrs, bcos::engine::OpForkId::Canyon);
    BOOST_REQUIRE(err);
    BOOST_CHECK(err->find("eip1559Params") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(PreHoloceneAttrsAcceptMissingEip1559Params)
{
    auto attrs = makeOpPayloadAttributes();
    attrs.eip1559Params.reset();
    attrs.minBaseFee.reset();
    BOOST_CHECK(
        !engine_common::op::validateOpPayloadAttributes(attrs, bcos::engine::OpForkId::Canyon));
}

BOOST_AUTO_TEST_SUITE_END()