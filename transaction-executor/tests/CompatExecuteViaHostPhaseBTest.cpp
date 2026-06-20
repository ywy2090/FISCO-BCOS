/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief TE Phase B: revision / feature / modexp / precompile / static-guard compat migration.
 *  @file CompatExecuteViaHostPhaseBTest.cpp
 */

#include "../../bcos-executor/test/unittest/evmone/compat/CompatTestFixture.h"
#include "bcos-evm/bcos/PrecompiledImpl.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-executor/src/Common.h"
#include "bcos-executor/src/vm/EvmPrecompiledAddress.h"
#include "bcos-executor/src/vm/VMInstance.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <boost/test/unit_test.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>

namespace bcos::test
{
namespace
{
using compat::compatMakeModexpInput;

bytes blsG1AddValidInputG1PlusP1()
{
    return bcos::fromHex(
        "0000000000000000000000000000000017f1d3a73197d7942695638c4fa9ac0fc3688c4f9774b905a14e3a3f17"
        "1bac586c55e83ff97a1aeffb3af00adb22c6bb"
        "0000000000000000000000000000000008b3f481e3aaa0f1a09e30ed741d8ae4fcf5e095d5d00af600db18cb2c"
        "04b3edd03cc744a2888ae40caa232946c5e7e1"
        "00000000000000000000000000000000112b98340eee2777cc3c14163dea3ec97977ac3dc5c70da32e6e87578f"
        "44912e902ccef9efe28d4a78b8999dfbca9426"
        "00000000000000000000000000000000186b28d92356c4dfec4b5201ad099dbdede3781f8998ddf929b4cd7756"
        "192185ca7b8f4ef7088f813270ac3d48868a21");
}

bytes blsG1AddExpectedG1PlusP1()
{
    return bcos::fromHex(
        "000000000000000000000000000000000a40300ce2dec9888b60690e9a41d3004fda4886854573974fab73b046"
        "d3147ba5b7a5bde85279ffede1b45b3918d82d"
        "0000000000000000000000000000000006d3d887e9f53b9ec4eb6cedf5607226754b07c01ace7834f57f3e7315"
        "faefb739e59018e22c492006190fba4a870025");
}

evmc_address precompileAddress(uint8_t suffix)
{
    evmc_address address{};
    address.bytes[19] = suffix;
    return address;
}

bcos::evm::EVMCResult callBuiltinAt(evmc_address const& recipient, bytesConstRef input,
    evmc_revision revision, int64_t gas = 10'000'000)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.recipient = recipient;
    message.code_address = recipient;
    message.gas = gas;
    message.input_data = input.data();
    message.input_size = input.size();

    bcos::evm_standard::RevisionConfig rev{.revision = revision};
    if (revision >= EVMC_OSAKA)
    {
        rev.eip7823 = true;
    }
    return bcos::evm::callBuiltinPrecompiled(message, rev, revision, true);
}

bytes copyOutput(bcos::evm::EVMCResult const& result)
{
    return {result.output_data, result.output_data + result.output_size};
}

bool fileContains(std::filesystem::path const& path, std::string_view needle)
{
    std::ifstream in(path);
    if (!in)
    {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str().find(needle) != std::string::npos;
}

std::optional<std::filesystem::path> findRepoFile(std::filesystem::path const& relative)
{
    auto cur = std::filesystem::path(__FILE__).parent_path();
    for (int i = 0; i < 8; ++i)
    {
        auto candidate = cur / relative;
        if (std::filesystem::exists(candidate))
        {
            return candidate;
        }
        auto parent = cur.parent_path();
        if (parent == cur)
        {
            break;
        }
        cur = parent;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> findBcosProtocolDirectory()
{
    auto cur = std::filesystem::path(__FILE__).parent_path();
    for (int i = 0; i < 8; ++i)
    {
        auto candidate = cur / "bcos-protocol";
        if (std::filesystem::is_directory(candidate))
        {
            return candidate;
        }
        auto parent = cur.parent_path();
        if (parent == cur)
        {
            break;
        }
        cur = parent;
    }
    return std::nullopt;
}

std::string_view trimLeading(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    {
        s.remove_prefix(1);
    }
    return s;
}

std::string removeCBlockCommentsOnLine(std::string_view line)
{
    std::string s(line);
    while (true)
    {
        const auto open = s.find("/*");
        if (open == std::string::npos)
        {
            break;
        }
        const auto close = s.find("*/", open + 2);
        if (close == std::string::npos)
        {
            s.erase(open);
            break;
        }
        s.erase(open, close - open + 2);
    }
    return s;
}

bool lineHasAuthorizationListOutsideSlashSlashComment(std::string_view line)
{
    std::string deblocked = removeCBlockCommentsOnLine(line);
    std::string_view work(deblocked);
    constexpr std::string_view needle = "authorization_list";
    auto trimmed = trimLeading(work);
    if (trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '/')
    {
        return false;
    }
    const auto slashPos = work.find("//");
    if (slashPos != std::string_view::npos)
    {
        work = work.substr(0, slashPos);
    }
    return work.find(needle) != std::string_view::npos;
}

bool sourceFileHasAuthorizationListOutsideComments(std::filesystem::path const& path)
{
    std::ifstream in(path);
    if (!in)
    {
        return false;
    }
    std::string line;
    while (std::getline(in, line))
    {
        if (lineHasAuthorizationListOutsideSlashSlashComment(line))
        {
            return true;
        }
    }
    return false;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(CompatExecuteViaHostPhaseB)

BOOST_AUTO_TEST_SUITE(CompatRevision)

BOOST_AUTO_TEST_CASE(TE_FC_B_R_cancun_without_prague)
{
    using namespace bcos::executor;
    BOOST_CHECK_EQUAL(toRevision(FiscoBcosScheduleCancun), EVMC_CANCUN);
    BOOST_CHECK(FiscoBcosScheduleCancun.enableCanCun);
    BOOST_CHECK(!FiscoBcosScheduleCancun.enablePrague);
    BOOST_CHECK(!FiscoBcosScheduleCancun.enableOsaka);
}

BOOST_AUTO_TEST_CASE(TE_FC_B_R_london_without_cancun)
{
    using namespace bcos::executor;
    BOOST_CHECK_EQUAL(toRevision(FiscoBcosSchedule), EVMC_LONDON);
    BOOST_CHECK(!FiscoBcosSchedule.enableCanCun);
    BOOST_CHECK(!FiscoBcosSchedule.enablePrague);
    BOOST_CHECK(!FiscoBcosSchedule.enableOsaka);
}

BOOST_AUTO_TEST_CASE(TE_FC_B_R_shanghai_via_pairs)
{
    using namespace bcos::executor;
    BOOST_CHECK_EQUAL(toRevision(FiscoBcosScheduleV320), EVMC_PARIS);
    BOOST_CHECK(FiscoBcosScheduleV320.enablePairs);
    BOOST_CHECK(!FiscoBcosScheduleV320.enableCanCun);
}

BOOST_AUTO_TEST_CASE(TE_FC_B_R_revision_priority_order)
{
    using namespace bcos::executor;
    BOOST_CHECK_EQUAL(toRevision(FiscoBcosScheduleOsaka), EVMC_OSAKA);
    BOOST_CHECK_EQUAL(toRevision(FiscoBcosSchedulePrague), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(toRevision(FiscoBcosScheduleCancun), EVMC_CANCUN);
    BOOST_CHECK_EQUAL(toRevision(FiscoBcosScheduleV320), EVMC_PARIS);
    BOOST_CHECK_EQUAL(toRevision(FiscoBcosSchedule), EVMC_LONDON);
}

BOOST_AUTO_TEST_CASE(TE_FC_B_R_features_block_version_ladder)
{
    using namespace bcos::executor;
    using bcos::protocol::BlockVersion;

    ledger::Features f;
    BOOST_CHECK_EQUAL(
        toRevision(f, static_cast<uint32_t>(BlockVersion::V3_0_VERSION)), EVMC_LONDON);
    BOOST_CHECK_EQUAL(toRevision(f, static_cast<uint32_t>(BlockVersion::V3_2_VERSION)), EVMC_PARIS);

    f.set(ledger::Features::Flag::feature_evm_cancun);
    BOOST_CHECK_EQUAL(
        toRevision(f, static_cast<uint32_t>(BlockVersion::V3_2_VERSION)), EVMC_CANCUN);

    f.set(ledger::Features::Flag::feature_evm_prague);
    BOOST_CHECK_EQUAL(
        toRevision(f, static_cast<uint32_t>(BlockVersion::V3_2_VERSION)), EVMC_PRAGUE);

    f.set(ledger::Features::Flag::feature_evm_osaka);
    BOOST_CHECK_EQUAL(toRevision(f, static_cast<uint32_t>(BlockVersion::V3_2_VERSION)), EVMC_OSAKA);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(CompatFeatureDefaults)

BOOST_AUTO_TEST_CASE(TE_FC_B_F_eip2929_default_off)
{
    ledger::Features f;
    BOOST_CHECK(!f.get(ledger::Features::Flag::feature_evm_eip2929));
    BOOST_CHECK(!f.get("feature_evm_eip2929"));
}

BOOST_AUTO_TEST_CASE(TE_FC_B_F_prague_default_off)
{
    ledger::Features f;
    BOOST_CHECK(!f.get(ledger::Features::Flag::feature_evm_prague));
}

BOOST_AUTO_TEST_CASE(TE_FC_B_F_osaka_default_off)
{
    ledger::Features f;
    BOOST_CHECK(!f.get(ledger::Features::Flag::feature_evm_osaka));
}

BOOST_AUTO_TEST_CASE(TE_FC_B_F_cancun_default_off)
{
    ledger::Features f;
    BOOST_CHECK(!f.get(ledger::Features::Flag::feature_evm_cancun));
}

BOOST_AUTO_TEST_CASE(TE_FC_B_F_flags_contain_string)
{
    BOOST_CHECK(ledger::Features::contains("feature_evm_cancun"));
    BOOST_CHECK(ledger::Features::contains("feature_evm_prague"));
    BOOST_CHECK(ledger::Features::contains("feature_evm_osaka"));
    BOOST_CHECK(ledger::Features::contains("feature_evm_eip2929"));
}

BOOST_AUTO_TEST_CASE(TE_FC_B_F_prague_does_not_auto_enable_cancun)
{
    ledger::Features f;
    f.set(ledger::Features::Flag::feature_evm_prague);
    BOOST_CHECK(!f.get(ledger::Features::Flag::feature_evm_cancun));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(CompatModexp)

BOOST_AUTO_TEST_CASE(TE_FC_B_M_modexp_compatibility)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
    auto const input = compatMakeModexpInput({0x02}, {0x08}, {0x0a});
    auto result = callBuiltinAt(precompileAddress(0x05), ref(input), EVMC_BERLIN);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    auto const output = copyOutput(result);
    BOOST_REQUIRE_EQUAL(output.size(), 1u);
    BOOST_CHECK_EQUAL(output[0], 6);
}

BOOST_AUTO_TEST_CASE(TE_FC_B_M_modexp_7_pow_0_mod_11)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
    bytes input(96, 0);
    input[31] = 1;
    input[63] = 1;
    input[95] = 1;
    input.push_back(0x07);
    input.push_back(0x00);
    input.push_back(0x0B);
    auto result = callBuiltinAt(precompileAddress(0x05), ref(input), EVMC_BERLIN);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    auto const output = copyOutput(result);
    BOOST_REQUIRE_EQUAL(output.size(), 1u);
    BOOST_CHECK_EQUAL(static_cast<unsigned>(output[0]), 1u);
}

BOOST_AUTO_TEST_CASE(TE_FC_B_M_modexp_mod_zero_empty)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
    bytes inputZeroMod(96, 0);
    inputZeroMod[31] = 1;
    inputZeroMod[63] = 1;
    inputZeroMod[95] = 0;
    inputZeroMod.push_back(0x02);
    inputZeroMod.push_back(0x03);
    auto result = callBuiltinAt(precompileAddress(0x05), ref(inputZeroMod), EVMC_BERLIN);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK(copyOutput(result).empty());
}

BOOST_AUTO_TEST_CASE(TE_FC_B_M_modexp_large_boundary)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
    bytes shortInput(96, 0);
    shortInput[31] = 1;
    shortInput[63] = 1;
    shortInput[95] = 7;
    auto result = callBuiltinAt(precompileAddress(0x05), ref(shortInput), EVMC_BERLIN);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    auto const output = copyOutput(result);
    BOOST_CHECK_EQUAL(output.size(), 7u);
    BOOST_CHECK(output == bytes(7, 0));
}

BOOST_AUTO_TEST_CASE(TE_FC_B_M_modexp_base_zero)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
    auto const input = compatMakeModexpInput({0x00}, {0x03}, {0x0b});
    auto result = callBuiltinAt(precompileAddress(0x05), ref(input), EVMC_BERLIN);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    auto const output = copyOutput(result);
    BOOST_REQUIRE_EQUAL(output.size(), 1u);
    BOOST_CHECK_EQUAL(output[0], 0);
}

BOOST_AUTO_TEST_CASE(TE_FC_B_M_modexp_exp_zero)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
    auto const input = compatMakeModexpInput({0x07}, {0x00}, {0x0b});
    auto result = callBuiltinAt(precompileAddress(0x05), ref(input), EVMC_BERLIN);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    auto const output = copyOutput(result);
    BOOST_REQUIRE_EQUAL(output.size(), 1u);
    BOOST_CHECK_EQUAL(output[0], 1);
}

BOOST_AUTO_TEST_CASE(TE_FC_B_M_modexp_right_padding)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
    bytes in(96, 0);
    in[31] = 2;
    in[63] = 1;
    in[95] = 1;
    in.push_back(0x02);
    in.push_back(0x00);
    in.push_back(0x03);
    in.push_back(0x05);
    auto result = callBuiltinAt(precompileAddress(0x05), ref(in), EVMC_BERLIN);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    auto const output = copyOutput(result);
    BOOST_REQUIRE_EQUAL(output.size(), 1u);
    BOOST_CHECK_EQUAL(output[0], 3);
}

BOOST_AUTO_TEST_CASE(TE_FC_B_M_modexp_mod_zero_nonzero_len)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
    bytes in(96, 0);
    in[31] = 1;
    in[63] = 1;
    in[95] = 3;
    in.push_back(0x02);
    in.push_back(0x03);
    in.push_back(0x00);
    in.push_back(0x00);
    in.push_back(0x00);
    auto result = callBuiltinAt(precompileAddress(0x05), ref(in), EVMC_BERLIN);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    auto const output = copyOutput(result);
    BOOST_CHECK_EQUAL(output.size(), 3u);
    BOOST_CHECK(output == bytes(3, 0));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(CompatPrecompileAddress)

BOOST_AUTO_TEST_CASE(TE_FC_B_P_bls_range_byte_parse_no_stoul)
{
    using bcos::executor::isBLSPrecompileAddress;
    BOOST_CHECK(isBLSPrecompileAddress("000000000000000000000000000000000000000b"));
    BOOST_CHECK(isBLSPrecompileAddress("0000000000000000000000000000000000000011"));
    BOOST_CHECK(isBLSPrecompileAddress("0x000000000000000000000000000000000000000f"));
    BOOST_CHECK(!isBLSPrecompileAddress("000000000000000000000000000000000000000a"));
    BOOST_CHECK(!isBLSPrecompileAddress("0000000000000000000000000000000000000012"));
    BOOST_CHECK(!isBLSPrecompileAddress("000000000000000000000000000000000000000B"));
    BOOST_CHECK(!isBLSPrecompileAddress("000000000000000000000000000000000000000g"));
}

BOOST_AUTO_TEST_CASE(TE_FC_B_P_p256verify_accepts_optional_0x_prefix)
{
    using bcos::executor::isP256verifyPrecompileAddress;
    BOOST_CHECK(isP256verifyPrecompileAddress("0000000000000000000000000000000000000100"));
    BOOST_CHECK(isP256verifyPrecompileAddress("0x0000000000000000000000000000000000000100"));
    BOOST_CHECK(!isP256verifyPrecompileAddress("0000000000000000000000000000000000000101"));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(CompatPrecompileBuiltin)

BOOST_AUTO_TEST_CASE(TE_FC_B_P_bls_ok_with_prague)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
    auto const input = blsG1AddValidInputG1PlusP1();
    auto const expected = blsG1AddExpectedG1PlusP1();
    auto result = callBuiltinAt(precompileAddress(0x0b), ref(input), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    auto const output = copyOutput(result);
    BOOST_REQUIRE_EQUAL(output.size(), expected.size());
    BOOST_CHECK(std::memcmp(output.data(), expected.data(), expected.size()) == 0);
}

BOOST_AUTO_TEST_CASE(TE_FC_B_P_bls_invalid_point_rejected_with_prague)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
    bytes invalid(256, 0xff);
    auto result = callBuiltinAt(precompileAddress(0x0b), ref(invalid), EVMC_PRAGUE);
    BOOST_CHECK(result.status_code == EVMC_INTERNAL_ERROR || result.status_code == EVMC_REVERT);
    BOOST_CHECK_EQUAL(result.output_size, 0);
}

BOOST_AUTO_TEST_CASE(TE_FC_B_P_bls_unknown_before_prague)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
    bytes emptyInput;
    auto result = callBuiltinAt(precompileAddress(0x0b), ref(emptyInput), EVMC_CANCUN);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_FAILURE);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(CompatStaticGuards)

BOOST_AUTO_TEST_CASE(TE_FC_B_G_no_system_call_block_start)
{
    auto const hostPathOpt = findRepoFile(
        std::filesystem::path("bcos-executor") / "src" / "vm" / "EVMHostInterface.cpp");
    BOOST_REQUIRE_MESSAGE(hostPathOpt.has_value(),
        "FC_G: EVMHostInterface.cpp must exist under bcos-executor/src/vm");
    auto const& hostPath = *hostPathOpt;
    BOOST_CHECK_MESSAGE(!fileContains(hostPath, "system_call_block_start"),
        "PoS system_call_block_start must not appear in executor host (FC-10)");
    BOOST_CHECK_MESSAGE(!fileContains(hostPath, "system_call_block_end"),
        "PoS system_call_block_end must not appear in executor host (FC-10)");
}

BOOST_AUTO_TEST_CASE(TE_FC_B_G_blob_not_applicable)
{
    BOOST_TEST_MESSAGE(
        "FC-09: EIP-7691 blob throughput N/A for PBFT FISCO-BCOS — no blob_count in block");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TE_FC_B_G_7685_requests_not_applicable)
{
    BOOST_TEST_MESSAGE("FC-08: EIP-7685 execution-layer requests are N/A on PBFT FISCO-BCOS.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TE_FC_B_G_7702_deferred)
{
    BOOST_TEST_MESSAGE("EIP-7702 (authorization_list) deferred — protocol field not wired.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TE_FC_B_G_no_authorization_list_in_protocol)
{
    const auto protoDirOpt = findBcosProtocolDirectory();
    BOOST_REQUIRE_MESSAGE(
        protoDirOpt.has_value(), "bcos-protocol must exist as a sibling of the repository root");
    const std::filesystem::path& protoDir = *protoDirOpt;

    constexpr std::size_t kMaxFiles = 5000;
    std::size_t scanned = 0;
    bool capHit = false;
    std::string firstHitPath;
    std::error_code iterEc;
    const auto dirOpts = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(protoDir, dirOpts, iterEc);
         !iterEc && it != std::filesystem::recursive_directory_iterator(); it.increment(iterEc))
    {
        std::error_code statEc;
        const auto& entry = *it;
        if (!entry.is_regular_file(statEc) || statEc)
        {
            continue;
        }
        const auto& p = entry.path();
        auto ext = p.extension().string();
        if (ext != ".h" && ext != ".cpp")
        {
            continue;
        }
        ++scanned;
        if (scanned > kMaxFiles)
        {
            capHit = true;
            break;
        }
        if (sourceFileHasAuthorizationListOutsideComments(p))
        {
            firstHitPath = p.string();
            break;
        }
    }
    BOOST_CHECK_MESSAGE(!iterEc, iterEc.message());
    if (capHit && firstHitPath.empty())
    {
        BOOST_FAIL("FC_G_no_authorization_list_in_protocol: scan cap exceeded");
    }
    BOOST_CHECK_MESSAGE(firstHitPath.empty(),
        "bcos-protocol must not use authorization_list outside // line comments; first hit: "
            << firstHitPath);
}

BOOST_AUTO_TEST_SUITE_END()
BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
