/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "NodeConfigLoaderProbe.h"
#include <bcos-framework/ledger/ChainMetadata.h>
#include <bcos-framework/ledger/OpForkScheduleCodec.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::tool;
using namespace bcos::ledger;

namespace bcos::test
{
namespace
{
struct OpstackScheduleProbe : public LoaderProbe
{
    using LoaderProbe::loadExecutorConfig;
    using LoaderProbe::loadGenesisFeatures;
    using LoaderProbe::loadOpstackConfig;
    using LoaderProbe::resolveOpForkSchedule;
};

constexpr char const* kNode =
    "1234567890123456789012345678901234567890123456789012345678901234"
    "1234567890123456789012345678901234567890123456789012345678901234";

std::string minimalGenesis(std::string const& extra = {})
{
    return "[version]\ncompatibility_version=3.18.0\n"
           "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
           "[web3]\nchain_id=1\n"
           "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
           "node.0=" +
           std::string(kNode) +
           ":1:1\n"
           "[tx]\ngas_limit=3000000000\n"
           "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
           "auth_admin_account=0x0000000000000000000000000000000000000001\n" +
           extra;
}

constexpr char const* kMainnetSchedule = "0:isthmus,1764691201:jovian,1783526401:karst";
constexpr char const* kKarstSchedule = "0:jovian,1781712001:karst";

crypto::HashType const kGenesisHash{
    "0x1111111111111111111111111111111111111111111111111111111111111111"};
}  // namespace

BOOST_AUTO_TEST_SUITE(NodeConfigOpstackScheduleTest)

BOOST_AUTO_TEST_CASE(newGenesisStoresCanonicalSchedule)
{
    OpstackScheduleProbe probe;
    auto pt =
        fromIni(minimalGenesis("[opstack]\nfork_schedule=" + std::string(kMainnetSchedule) + "\n"));
    probe.loadGenesisFeatures(pt);
    probe.loadOpstackConfig(pt);
    BOOST_REQUIRE(probe.genesisConfig().m_opstackForkSchedule.has_value());
    BOOST_CHECK_EQUAL(*probe.genesisConfig().m_opstackForkSchedule, kMainnetSchedule);
}

BOOST_AUTO_TEST_CASE(scheduleAndFeatureOpJovianRejected)
{
    OpstackScheduleProbe probe;
    auto pt = fromIni(minimalGenesis("[features]\nfeature_op_jovian=1\n[opstack]\nfork_schedule=" +
                                     std::string(kMainnetSchedule) + "\n"));
    probe.loadGenesisFeatures(pt);
    BOOST_CHECK_THROW(probe.loadOpstackConfig(pt), InvalidConfig);
}

BOOST_AUTO_TEST_CASE(scheduleAndEvmRevisionRejected)
{
    OpstackScheduleProbe probe;
    auto pt = fromIni(minimalGenesis(
        "evm_revision=cancun\n[opstack]\nfork_schedule=" + std::string(kMainnetSchedule) + "\n"));
    probe.loadGenesisFeatures(pt);
    probe.loadExecutorConfig(pt);
    BOOST_CHECK_THROW(probe.loadOpstackConfig(pt), InvalidConfig);
}

BOOST_AUTO_TEST_CASE(scheduleAndEvmRevisionForksRejected)
{
    OpstackScheduleProbe probe;
    auto pt =
        fromIni(minimalGenesis("evm_revision_forks=0:cancun,100000:osaka\n[opstack]\n"
                               "fork_schedule=" +
                               std::string(kMainnetSchedule) + "\n"));
    probe.loadGenesisFeatures(pt);
    probe.loadExecutorConfig(pt);
    BOOST_CHECK_THROW(probe.loadOpstackConfig(pt), InvalidConfig);
}

BOOST_AUTO_TEST_CASE(existingDbWithoutMetadataRejectsIniKarst)
{
    OpstackScheduleProbe probe;
    auto pt =
        fromIni(minimalGenesis("[opstack]\nfork_schedule=" + std::string(kKarstSchedule) + "\n"));
    probe.loadGenesisFeatures(pt);
    probe.loadOpstackConfig(pt);
    BOOST_CHECK_THROW(probe.resolveOpForkSchedule(std::nullopt, true, kGenesisHash), InvalidConfig);
}

BOOST_AUTO_TEST_CASE(existingDbWithoutMetadataUsesLegacyMemoryOnly)
{
    OpstackScheduleProbe probe;
    auto pt = fromIni(minimalGenesis("[features]\nfeature_op_jovian=1\n"));
    probe.loadGenesisFeatures(pt);
    probe.loadOpstackConfig(pt);
    BOOST_REQUIRE_NO_THROW(probe.resolveOpForkSchedule(std::nullopt, true, kGenesisHash));
    BOOST_REQUIRE(probe.opForkScheduleRuntime());
    BOOST_CHECK_EQUAL(probe.opForkScheduleRuntime()->canonical, "0:jovian");
    BOOST_CHECK(probe.opForkScheduleRuntime()->legacyMemoryOnly);
}

BOOST_AUTO_TEST_CASE(existingDbWithoutMetadataDefaultsToIsthmusLegacy)
{
    OpstackScheduleProbe probe;
    auto pt = fromIni(minimalGenesis());
    probe.loadGenesisFeatures(pt);
    probe.loadOpstackConfig(pt);
    BOOST_REQUIRE_NO_THROW(probe.resolveOpForkSchedule(std::nullopt, true, kGenesisHash));
    BOOST_REQUIRE(probe.opForkScheduleRuntime());
    BOOST_CHECK_EQUAL(probe.opForkScheduleRuntime()->canonical, "0:isthmus");
    BOOST_CHECK(probe.opForkScheduleRuntime()->legacyMemoryOnly);
}

BOOST_AUTO_TEST_CASE(metadataAuthoritativeRejectsMismatchedIni)
{
    OpstackScheduleProbe probe;
    auto pt =
        fromIni(minimalGenesis("[opstack]\nfork_schedule=" + std::string(kKarstSchedule) + "\n"));
    probe.loadGenesisFeatures(pt);
    probe.loadOpstackConfig(pt);

    OpForkScheduleMetadata metadata{
        .schedule = kMainnetSchedule,
        .scheduleHash = keccakOpForkScheduleHash(kMainnetSchedule),
        .genesisHash = kGenesisHash,
    };
    BOOST_CHECK_THROW(probe.resolveOpForkSchedule(metadata, true, kGenesisHash), InvalidConfig);
}

BOOST_AUTO_TEST_CASE(metadataAuthoritativeAcceptsMatchingIni)
{
    OpstackScheduleProbe probe;
    auto pt =
        fromIni(minimalGenesis("[opstack]\nfork_schedule=" + std::string(kMainnetSchedule) + "\n"));
    probe.loadGenesisFeatures(pt);
    probe.loadOpstackConfig(pt);

    OpForkScheduleMetadata metadata{
        .schedule = kMainnetSchedule,
        .scheduleHash = keccakOpForkScheduleHash(kMainnetSchedule),
        .genesisHash = kGenesisHash,
    };
    BOOST_REQUIRE_NO_THROW(probe.resolveOpForkSchedule(metadata, true, kGenesisHash));
    BOOST_REQUIRE(probe.opForkScheduleRuntime());
    BOOST_CHECK_EQUAL(probe.opForkScheduleRuntime()->canonical, kMainnetSchedule);
    BOOST_CHECK(!probe.opForkScheduleRuntime()->legacyMemoryOnly);
}

BOOST_AUTO_TEST_CASE(metadataWithBadHashRejectedAtResolve)
{
    OpstackScheduleProbe probe;
    auto pt = fromIni(minimalGenesis());
    probe.loadGenesisFeatures(pt);
    probe.loadOpstackConfig(pt);

    OpForkScheduleMetadata metadata{
        .schedule = kMainnetSchedule,
        .scheduleHash = crypto::HashType("0x00"),
        .genesisHash = kGenesisHash,
    };
    BOOST_CHECK_THROW(probe.resolveOpForkSchedule(metadata, true, kGenesisHash), InvalidConfig);
}

BOOST_AUTO_TEST_CASE(partialMetadataTripleRejected)
{
    OpForkScheduleMetadataRows rows{.schedule = kMainnetSchedule};
    BOOST_CHECK_THROW(
        (void)validateOpForkScheduleMetadataRows(rows, kGenesisHash), InvalidOpForkSchedule);
}

BOOST_AUTO_TEST_CASE(hashMismatchRejected)
{
    OpForkScheduleMetadataRows rows{
        .schedule = kMainnetSchedule,
        .scheduleHash = crypto::HashType("0x00").hex(),
        .genesisHash = kGenesisHash.hex(),
    };
    BOOST_CHECK_THROW(
        (void)validateOpForkScheduleMetadataRows(rows, kGenesisHash), InvalidOpForkSchedule);
}

BOOST_AUTO_TEST_CASE(genesisBindingMismatchRejected)
{
    OpForkScheduleMetadataRows rows{
        .schedule = kMainnetSchedule,
        .scheduleHash = keccakOpForkScheduleHash(kMainnetSchedule).hex(),
        .genesisHash = crypto::HashType("0x22").hex(),
    };
    BOOST_CHECK_THROW(
        (void)validateOpForkScheduleMetadataRows(rows, kGenesisHash), InvalidOpForkSchedule);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
