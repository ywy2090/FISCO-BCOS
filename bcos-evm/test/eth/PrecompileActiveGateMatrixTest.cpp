#define BOOST_TEST_MODULE PrecompileActiveGateMatrixTest

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/precompiled/PrecompileActive.h"
#include "bcos-evm/eth/precompiled/PrecompiledAddress.h"
#include <boost/test/included/unit_test.hpp>
#include <string>

namespace bcos::evm::test
{
namespace
{
evmc_address precompileAddress(uint8_t lowByte, uint8_t highByte = 0x00)
{
    evmc_address addr{};
    addr.bytes[18] = highByte;
    addr.bytes[19] = lowByte;
    return addr;
}
}  // namespace

BOOST_AUTO_TEST_CASE(isActivePrecompile_gate_matrix)
{
    struct Row
    {
        uint8_t lowByte;
        uint8_t highByte;
        evmc_revision revision;
        bool eip2537;
        bool eip7212;
        bool expected;
    };

    Row const rows[] = {
        {0x01, 0x00, EVMC_ISTANBUL, false, false, false},
        {0x04, 0x00, EVMC_BERLIN, false, false, true},
        {0x09, 0x00, EVMC_BERLIN, false, false, true},
        {0x0a, 0x00, EVMC_SHANGHAI, false, false, false},
        {0x0a, 0x00, EVMC_CANCUN, false, false, true},
        {0x0b, 0x00, EVMC_CANCUN, false, false, false},
        {0x0b, 0x00, EVMC_PRAGUE, true, false, true},
        {0x0b, 0x00, EVMC_PRAGUE, false, false, false},
        {0x11, 0x00, EVMC_PRAGUE, true, false, true},
        {0x00, 0x01, EVMC_OSAKA, false, true, true},
        {0x00, 0x01, EVMC_OSAKA, false, false, false},
        {0x00, 0x01, EVMC_PRAGUE, false, false, false},
    };

    for (auto const& row : rows)
    {
        bcos::evm::RevisionConfig cfg{
            .revision = row.revision, .eip2537 = row.eip2537, .eip7212 = row.eip7212};
        auto const addr = precompileAddress(row.lowByte, row.highByte);
        auto const suffix = bcos::evm::precompileSuffix(addr);
        if (row.highByte == 0x00 && row.lowByte >= 0x01 && row.lowByte <= 0x11)
        {
            BOOST_REQUIRE(suffix.has_value());
        }
        if (row.highByte == 0x01 && row.lowByte == 0x00)
        {
            BOOST_REQUIRE(suffix.has_value());
            BOOST_CHECK_EQUAL(
                *suffix, static_cast<uint16_t>(bcos::evm::P256VERIFY_PRECOMPILE_INDEX));
        }
        BOOST_CHECK_MESSAGE(bcos::evm::precompiled::isActivePrecompile(cfg, addr) == row.expected,
            "gate mismatch for suffix 0x" + std::to_string(row.highByte) +
                std::to_string(row.lowByte));
    }
}

BOOST_AUTO_TEST_CASE(isActivePrecompile_rejects_non_precompile_address)
{
    bcos::evm::RevisionConfig cfg = bcos::evm::revisionConfigFromRevision(EVMC_PRAGUE);
    auto const addr = precompileAddress(0x42);
    BOOST_CHECK(!bcos::evm::precompileSuffix(addr).has_value());
    BOOST_CHECK(!bcos::evm::precompiled::isActivePrecompile(cfg, addr));
}

BOOST_AUTO_TEST_CASE(precompileSuffix_matches_low_and_p256_ranges)
{
    auto const identity = precompileAddress(0x04);
    auto const p256 = precompileAddress(0x00, 0x01);
    auto const random = precompileAddress(0x42);

    BOOST_REQUIRE(bcos::evm::precompileSuffix(identity).has_value());
    BOOST_CHECK_EQUAL(*bcos::evm::precompileSuffix(identity), 0x0004u);
    BOOST_REQUIRE(bcos::evm::precompileSuffix(p256).has_value());
    BOOST_CHECK_EQUAL(*bcos::evm::precompileSuffix(p256),
        static_cast<uint16_t>(bcos::evm::P256VERIFY_PRECOMPILE_INDEX));
    BOOST_CHECK(!bcos::evm::precompileSuffix(random).has_value());
}

}  // namespace bcos::evm::test
