/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Characterization tests for FISCO CREATE address derivation (ADR-022).
 * @file FiscoAddressDerivationTest.cpp
 *
 * Documents current seam behavior and open-question oracles before FiscoAddressDerivation
 * consolidation. See bcos-evm/docs/adr/022-fisco-create-address-derivation.md §4 + OQ1–OQ3.
 */

#define BOOST_TEST_MODULE FiscoAddressDerivationTest
#include "bcos-crypto/ChecksumAddress.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/bcos/FiscoConstants.h"
#include "bcos-evm/bcos/FiscoPolicy.h"
#include "bcos-evm/bcos/FiscoTxAdapter.h"
#include "bcos-evm/bcos/FiscoVmHostPolicy.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "state/InMemoryEvmStateReader.h"
#include <fmt/compile.h>
#include <fmt/format.h>
#include <boost/test/included/unit_test.hpp>
#include <algorithm>
#include <cstring>

namespace bcos::evm::test
{
namespace
{
using bcos::bytesConstRef;
using bcos::u256;

crypto::Keccak256 const& keccakHashImpl()
{
    static crypto::Keccak256 instance;
    return instance;
}

evmc_address addressFromTailByte(uint8_t value)
{
    evmc_address out{};
    out.bytes[19] = value;
    return out;
}

bool addressEqual(evmc_address const& lhs, evmc_address const& rhs)
{
    return std::memcmp(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
}

evmc_address legacyAddressFromSenderNonce(evmc_address const& sender, u256 const& nonce)
{
    auto const raw = newLegacyEVMAddress(bytesConstRef(sender.bytes, sizeof(sender.bytes)), nonce);
    evmc_address out{};
    std::copy(raw.begin(), raw.end(), out.bytes);
    return out;
}

evmc_address fiscoHashAddress(
    crypto::Hash const& hashImpl, int64_t blockNumber, int64_t contextID, int64_t seq)
{
    auto const label = fmt::format(FMT_COMPILE("{}_{}_{}"), blockNumber, contextID, seq);
    auto const digest = hashImpl.hash(label);
    evmc_address out{};
    std::copy_n(digest.data(), sizeof(out.bytes), out.bytes);
    return out;
}

evmc_address fiscoCreate2Address(crypto::Hash const& hashImpl, evmc_address const& deployer,
    evmc_bytes32 const& salt, bytesConstRef initCode)
{
    std::array<bcos::byte, 1 + sizeof(deployer.bytes) + sizeof(salt.bytes) + crypto::HashType::SIZE>
        buffer{};
    uint8_t* ptr = buffer.data();
    *ptr++ = 0xff;
    ptr = std::uninitialized_copy_n(deployer.bytes, sizeof(deployer.bytes), ptr);
    auto const saltBE = toBigEndian(bcos::evm::state::fromEvmC(salt));
    ptr = std::uninitialized_copy(saltBE.begin(), saltBE.end(), ptr);
    auto const inputHash = hashImpl.hash(initCode);
    ptr = std::uninitialized_copy(inputHash.begin(), inputHash.end(), ptr);
    auto const addressHash = hashImpl.hash(bytesConstRef(buffer.data(), buffer.size()));
    evmc_address out{};
    std::copy_n(addressHash.begin() + 12, sizeof(out.bytes), out.bytes);
    return out;
}

evmc_message makeEmptyCreateMessage(evmc_address sender, int32_t depth = 0)
{
    evmc_message message{};
    message.kind = EVMC_CREATE;
    message.depth = depth;
    message.sender = sender;
    message.code_address = EMPTY_EVM_ADDRESS;
    return message;
}

evmc_message runTopLevelDerive(FiscoTxAdapterInput const& input)
{
    return deriveMessage(input);
}

struct NestedPrepareResult
{
    evmc_message message;
    int64_t seqAfter{0};
};

NestedPrepareResult runNestedPrepare(FiscoVmHostPolicy::FiscoVmHostPolicyDeps deps,
    evmc_message message, evmc_revision revision = EVMC_CANCUN)
{
    NestedPrepareResult out{.message = message};
    FiscoVmHostPolicy extension(/*skipEvmNativeValueTransfer*/ true, std::move(deps));
    extension.prepareMessage(revision, out.message);
    if (deps.seq != nullptr)
    {
        out.seqAfter = *deps.seq;
    }
    return out;
}

ledger::LedgerConfig ledgerWithFeature(ledger::Features::Flag flag)
{
    ledger::LedgerConfig config;
    ledger::Features features;
    features.set(flag);
    config.setFeatures(std::move(features));
    return config;
}

// OQ3: deterministic non-Keccak oracle for hashImpl sensitivity characterization.
class LabeledHash final : public crypto::Hash
{
public:
    explicit LabeledHash(std::string tag) : m_tag(std::move(tag)) {}

    crypto::HashType hash(bytesConstRef input) const override
    {
        crypto::HashType out{};
        out[0] = static_cast<uint8_t>(m_tag.size());
        out[1] = static_cast<uint8_t>(input.size());
        std::memcpy(out.data() + 2, m_tag.data(), std::min(m_tag.size(), size_t{30}));
        return out;
    }

    bcos::crypto::hasher::AnyHasher hasher() const override { return {}; }

private:
    std::string m_tag;
};

}  // namespace

// --- Baseline: top-level seam (deriveMessage) --------------------------------

BOOST_AUTO_TEST_SUITE(FiscoAddressDerivationTopLevelCharacterization)

BOOST_AUTO_TEST_CASE(derive_message_matches_fisco_policy_derive)
{
    auto const sender = addressFromTailByte(0x11);
    auto const message = makeEmptyCreateMessage(sender);
    auto const& hashImpl = keccakHashImpl();

    auto const fromBridge = runTopLevelDerive(FiscoTxAdapterInput{.web3Tx = false,
        .message = message,
        .blockNumber = 42,
        .contextID = 7,
        .seq = 3,
        .nonce = u256{0},
        .hashImpl = &hashImpl});

    auto const fromPolicy = bcos::chain_policy::FiscoPolicy::deriveMessage(
        false, false, message, 42, 7, 3, u256{0}, hashImpl);

    BOOST_CHECK(addressEqual(fromBridge.code_address, fromPolicy.code_address));
    BOOST_CHECK(addressEqual(fromBridge.recipient, fromPolicy.recipient));
}

BOOST_AUTO_TEST_CASE(top_level_fisco_hash_uses_block_context_seq)
{
    auto const sender = addressFromTailByte(0x21);
    auto const message = makeEmptyCreateMessage(sender);
    auto const& hashImpl = keccakHashImpl();
    auto const expected = fiscoHashAddress(hashImpl, 100, 9, 2);

    auto const resolved = runTopLevelDerive(FiscoTxAdapterInput{.web3Tx = false,
        .message = message,
        .blockNumber = 100,
        .contextID = 9,
        .seq = 2,
        .nonce = u256{0},
        .hashImpl = &hashImpl});

    BOOST_CHECK(addressEqual(resolved.code_address, expected));
}

BOOST_AUTO_TEST_CASE(top_level_legacy_web3_uses_tx_nonce)
{
    auto const sender = addressFromTailByte(0x31);
    auto const message = makeEmptyCreateMessage(sender);
    auto const& hashImpl = keccakHashImpl();
    u256 const txNonce{5};
    auto const expected = legacyAddressFromSenderNonce(sender, txNonce);

    auto const resolved = runTopLevelDerive(FiscoTxAdapterInput{.web3Tx = true,
        .message = message,
        .blockNumber = 1,
        .contextID = 0,
        .seq = 0,
        .nonce = txNonce,
        .hashImpl = &hashImpl});

    BOOST_CHECK(addressEqual(resolved.code_address, expected));
}

BOOST_AUTO_TEST_CASE(top_level_create_skips_when_code_address_prefilled)
{
    auto const sender = addressFromTailByte(0x41);
    auto message = makeEmptyCreateMessage(sender);
    message.code_address = addressFromTailByte(0xAA);
    auto const& hashImpl = keccakHashImpl();

    auto const resolved = runTopLevelDerive(FiscoTxAdapterInput{.web3Tx = false,
        .message = message,
        .blockNumber = 1,
        .contextID = 0,
        .seq = 99,
        .nonce = u256{0},
        .hashImpl = &hashImpl});

    BOOST_CHECK(addressEqual(resolved.code_address, message.code_address));
}

// ADR-022 D1: top-level honors feature_evm_address (aligned with nested).
BOOST_AUTO_TEST_CASE(top_level_feature_evm_address_enables_legacy_without_web3_tx)
{
    auto const sender = addressFromTailByte(0x51);
    auto const message = makeEmptyCreateMessage(sender);
    auto const& hashImpl = keccakHashImpl();
    auto const legacyExpected = legacyAddressFromSenderNonce(sender, u256{3});

    auto const resolved = runTopLevelDerive(FiscoTxAdapterInput{.web3Tx = false,
        .featureEvmAddress = true,
        .message = message,
        .blockNumber = 11,
        .contextID = 2,
        .seq = 4,
        .nonce = u256{3},
        .hashImpl = &hashImpl});

    BOOST_CHECK(addressEqual(resolved.code_address, legacyExpected));
}

BOOST_AUTO_TEST_SUITE_END()

// --- Baseline: nested seam (FiscoVmHostPolicy::prepareMessage) ---------------

BOOST_AUTO_TEST_SUITE(FiscoAddressDerivationNestedCharacterization)

BOOST_AUTO_TEST_CASE(nested_fisco_hash_increments_nested_seq)
{
    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    auto const origin = addressFromTailByte(0x61);
    auto const contract = addressFromTailByte(0x62);
    auto message = makeEmptyCreateMessage(contract, /*depth=*/1);
    auto const& hashImpl = keccakHashImpl();

    int64_t nestedSeq = 5;
    auto const expected = fiscoHashAddress(hashImpl, 200, 3, 6);

    ledger::LedgerConfig ledgerConfig;
    FiscoVmHostPolicy::FiscoVmHostPolicyDeps deps;
    deps.state = &state;
    deps.blockNumber = 200;
    deps.contextID = 3;
    deps.seq = &nestedSeq;
    deps.hashImpl = &hashImpl;
    deps.origin = origin;
    deps.revisionFlags.web3Tx = false;
    deps.ledgerConfig = &ledgerConfig;

    auto const out = runNestedPrepare(std::move(deps), message);
    BOOST_CHECK_EQUAL(nestedSeq, 6);
    BOOST_CHECK(addressEqual(out.message.code_address, expected));
}

BOOST_AUTO_TEST_CASE(nested_legacy_web3_reads_state_nonce_not_tx_nonce_param)
{
    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    auto const origin = addressFromTailByte(0x71);
    auto const contract = addressFromTailByte(0x72);
    u256 const stateNonce{8};
    state.set_nonce(contract, 8);

    auto message = makeEmptyCreateMessage(contract, /*depth=*/1);
    auto const& hashImpl = keccakHashImpl();
    auto const expected = legacyAddressFromSenderNonce(contract, stateNonce);
    auto const txNonceOracle = legacyAddressFromSenderNonce(contract, u256{1});

    int64_t nestedSeq = 0;
    ledger::LedgerConfig ledgerConfig;
    FiscoVmHostPolicy::FiscoVmHostPolicyDeps deps;
    deps.state = &state;
    deps.hashImpl = &hashImpl;
    deps.origin = origin;
    deps.seq = &nestedSeq;
    deps.revisionFlags.web3Tx = true;
    deps.ledgerConfig = &ledgerConfig;

    auto const out = runNestedPrepare(std::move(deps), message);
    BOOST_CHECK(addressEqual(out.message.code_address, expected));
    BOOST_CHECK(!addressEqual(out.message.code_address, txNonceOracle));
}

BOOST_AUTO_TEST_CASE(nested_feature_evm_address_enables_legacy_without_web3_tx)
{
    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    auto const origin = addressFromTailByte(0x81);
    auto const contract = addressFromTailByte(0x82);
    state.set_nonce(contract, 2);

    auto message = makeEmptyCreateMessage(contract, /*depth=*/1);
    auto const& hashImpl = keccakHashImpl();
    auto const expected = legacyAddressFromSenderNonce(contract, u256{2});

    int64_t nestedSeq = 0;
    auto ledgerConfig = ledgerWithFeature(ledger::Features::Flag::feature_evm_address);
    FiscoVmHostPolicy::FiscoVmHostPolicyDeps deps;
    deps.state = &state;
    deps.hashImpl = &hashImpl;
    deps.origin = origin;
    deps.seq = &nestedSeq;
    deps.revisionFlags.web3Tx = false;
    deps.ledgerConfig = &ledgerConfig;

    auto const out = runNestedPrepare(std::move(deps), message);
    BOOST_CHECK(addressEqual(out.message.code_address, expected));
}

BOOST_AUTO_TEST_CASE(top_level_depth_zero_skips_nested_prepare_derivation)
{
    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    auto const origin = addressFromTailByte(0x91);
    auto message = makeEmptyCreateMessage(origin, /*depth=*/0);
    auto const& hashImpl = keccakHashImpl();

    int64_t nestedSeq = 0;
    ledger::LedgerConfig ledgerConfig;
    FiscoVmHostPolicy::FiscoVmHostPolicyDeps deps;
    deps.state = &state;
    deps.hashImpl = &hashImpl;
    deps.origin = origin;
    deps.seq = &nestedSeq;
    deps.revisionFlags.web3Tx = false;
    deps.ledgerConfig = &ledgerConfig;

    auto const out = runNestedPrepare(std::move(deps), message);
    BOOST_CHECK(addressEqual(out.message.code_address, EMPTY_EVM_ADDRESS));
    BOOST_CHECK_EQUAL(nestedSeq, 0);
}

BOOST_AUTO_TEST_SUITE_END()

// --- Open question oracles (ADR-022 OQ1–OQ3) ---------------------------------

BOOST_AUTO_TEST_SUITE(FiscoAddressDerivationOpenQuestions)

// OQ1: nested legacy nonce — VmHostPolicy(state) vs Policy::deriveMessage(tx nonce).
BOOST_AUTO_TEST_CASE(oq1_nested_legacy_nonce_oracle_diverges_from_policy_derive)
{
    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    auto const origin = addressFromTailByte(0xA1);
    auto const contract = addressFromTailByte(0xA2);
    state.set_nonce(contract, 4);

    auto message = makeEmptyCreateMessage(contract, /*depth=*/1);
    auto const& hashImpl = keccakHashImpl();

    int64_t nestedSeq = 0;
    ledger::LedgerConfig ledgerConfig;
    FiscoVmHostPolicy::FiscoVmHostPolicyDeps deps;
    deps.state = &state;
    deps.hashImpl = &hashImpl;
    deps.origin = origin;
    deps.seq = &nestedSeq;
    deps.revisionFlags.web3Tx = true;
    deps.ledgerConfig = &ledgerConfig;
    auto const vmHostAddr = runNestedPrepare(std::move(deps), message).message.code_address;

    u256 const txNonceParam{1};
    auto const policyAddr = bcos::chain_policy::FiscoPolicy::deriveMessage(
        true, false, message, 1, 0, /*seq=*/nestedSeq + 1, txNonceParam, hashImpl)
                                .code_address;

    BOOST_CHECK(!addressEqual(vmHostAddr, policyAddr));
    BOOST_CHECK(addressEqual(vmHostAddr, legacyAddressFromSenderNonce(contract, u256{4})));
    BOOST_CHECK(addressEqual(policyAddr, legacyAddressFromSenderNonce(contract, txNonceParam)));
}

// OQ2: CREATE2 deployer — top-level always sender; nested prefers callerAddress.
BOOST_AUTO_TEST_CASE(oq2_create2_deployer_top_level_uses_sender_only)
{
    auto const sender = addressFromTailByte(0xB1);
    auto const caller = addressFromTailByte(0xB2);
    evmc_bytes32 salt{};
    salt.bytes[31] = 0x01;
    bcos::bytes initCode{0x60, 0x00, 0x60, 0x00, 0xF3};

    evmc_message message{};
    message.kind = EVMC_CREATE2;
    message.sender = sender;
    message.create2_salt = salt;
    message.input_data = initCode.data();
    message.input_size = initCode.size();

    auto const& hashImpl = keccakHashImpl();
    auto const withSender = fiscoCreate2Address(
        hashImpl, sender, salt, bytesConstRef(initCode.data(), initCode.size()));

    auto const resolved = runTopLevelDerive(FiscoTxAdapterInput{.web3Tx = false,
        .message = message,
        .blockNumber = 1,
        .contextID = 0,
        .seq = 0,
        .nonce = u256{0},
        .hashImpl = &hashImpl});

    BOOST_CHECK(addressEqual(resolved.code_address, withSender));
    BOOST_CHECK(
        !addressEqual(resolved.code_address, fiscoCreate2Address(hashImpl, caller, salt,
                                                 bytesConstRef(initCode.data(), initCode.size()))));
}

BOOST_AUTO_TEST_CASE(oq2_create2_deployer_nested_prefers_caller_address)
{
    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    auto const origin = addressFromTailByte(0xC1);
    auto const contract = addressFromTailByte(0xC2);
    auto const caller = addressFromTailByte(0xC3);
    evmc_bytes32 salt{};
    salt.bytes[31] = 0x02;
    bcos::bytes initCode{0x60, 0x00, 0x60, 0x00, 0xF3};

    evmc_message message{};
    message.kind = EVMC_CREATE2;
    message.depth = 1;
    message.sender = contract;
    message.create2_salt = salt;
    message.input_data = initCode.data();
    message.input_size = initCode.size();

    auto const& hashImpl = keccakHashImpl();
    auto const expected = fiscoCreate2Address(
        hashImpl, caller, salt, bytesConstRef(initCode.data(), initCode.size()));

    int64_t nestedSeq = 0;
    ledger::LedgerConfig ledgerConfig;
    FiscoVmHostPolicy::FiscoVmHostPolicyDeps deps;
    deps.state = &state;
    deps.hashImpl = &hashImpl;
    deps.origin = origin;
    deps.seq = &nestedSeq;
    deps.ledgerConfig = &ledgerConfig;

    FiscoVmHostPolicy extension(/*skipEvmNativeValueTransfer*/ true, std::move(deps));
    extension.setCallerAddress(caller);
    extension.prepareMessage(EVMC_CANCUN, message);

    BOOST_CHECK(addressEqual(message.code_address, expected));
}

// OQ3: hashImpl algorithm — FISCO-hash / CREATE2 digest follow injected Hash, not hard-coded
// Keccak.
BOOST_AUTO_TEST_CASE(oq3_fisco_hash_address_depends_on_hash_impl)
{
    auto const sender = addressFromTailByte(0xD1);
    auto const message = makeEmptyCreateMessage(sender);
    LabeledHash labeledHash("fisco-hash-oq3");

    auto const keccakResolved = runTopLevelDerive(FiscoTxAdapterInput{.web3Tx = false,
        .message = message,
        .blockNumber = 5,
        .contextID = 1,
        .seq = 1,
        .nonce = u256{0},
        .hashImpl = &keccakHashImpl()});

    auto const labeledResolved = runTopLevelDerive(FiscoTxAdapterInput{.web3Tx = false,
        .message = message,
        .blockNumber = 5,
        .contextID = 1,
        .seq = 1,
        .nonce = u256{0},
        .hashImpl = &labeledHash});

    BOOST_CHECK(!addressEqual(keccakResolved.code_address, labeledResolved.code_address));
    BOOST_CHECK(addressEqual(labeledResolved.code_address, fiscoHashAddress(labeledHash, 5, 1, 1)));
}

BOOST_AUTO_TEST_CASE(oq3_create2_initcode_digest_uses_hash_impl_not_eth_keccak)
{
    auto const sender = addressFromTailByte(0xD2);
    evmc_bytes32 salt{};
    salt.bytes[31] = 0x03;
    bcos::bytes initCode{0x60, 0x80, 0x60, 0x40, 0x52};

    LabeledHash labeledHash("create2-oq3");
    auto const labeledExpected = fiscoCreate2Address(
        labeledHash, sender, salt, bytesConstRef(initCode.data(), initCode.size()));

    evmc_message message{};
    message.kind = EVMC_CREATE2;
    message.sender = sender;
    message.create2_salt = salt;
    message.input_data = initCode.data();
    message.input_size = initCode.size();

    auto const resolved = runTopLevelDerive(FiscoTxAdapterInput{.web3Tx = false,
        .message = message,
        .blockNumber = 0,
        .contextID = 0,
        .seq = 0,
        .nonce = u256{0},
        .hashImpl = &labeledHash});

    BOOST_CHECK(addressEqual(resolved.code_address, labeledExpected));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::evm::test
