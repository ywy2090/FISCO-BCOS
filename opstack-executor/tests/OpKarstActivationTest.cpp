// OpKarstActivationSuite — pinned Karst NUT bundle fixture, activation-block ordering,
// EIP-7825 ordinary/deposit gas rules, and header gas-limit preservation (Task 9).
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <evmone/evmone.h>
#include <json/json.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpDepositEncode.h>
#include <boost/test/unit_test.hpp>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <test/utils/test_state.hpp>

using namespace bcos::evm::opstack;

namespace
{
constexpr char kPinnedBundleSha256[] =
    "08f5df36f7ff29a9421ab84beebe96684a7455df9a5d234a2468613a4008ad91";
constexpr uint64_t kSteadyStateGasLimit = 60'000'000;
constexpr auto kSender = 0x00000000000000000000000000000000000000aa_address;

#ifndef OP_KARST_NUT_BUNDLE_PATH
#define OP_KARST_NUT_BUNDLE_PATH "opstack-executor/tests/fixtures/karst_nut_bundle.json"
#endif

std::string sha256Hex(std::string const& path)
{
    std::ifstream in(path, std::ios::binary);
    BOOST_REQUIRE_MESSAGE(in, "cannot open fixture: " << path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    BOOST_REQUIRE(EVP_Digest(content.data(), content.size(), digest, &len, EVP_sha256(), nullptr));
    std::ostringstream oss;
    for (unsigned int i = 0; i < len; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    return oss.str();
}

struct NutBundle
{
    std::vector<std::string> qualifiedIntents;
    std::vector<DepositTx> deposits;
    uint64_t upgradeGas = 0;
};

evmc::address parseAddress(std::string const& hex)
{
    auto bytes = bcos::fromHex(hex);
    evmc::address out{};
    std::memcpy(out.bytes + (sizeof(out.bytes) - bytes.size()), bytes.data(), bytes.size());
    return out;
}

evmc::bytes parseData(std::string const& hex)
{
    auto bytes = bcos::fromHex(hex);
    return evmc::bytes(bytes.begin(), bytes.end());
}

NutBundle loadKarstNutBundle(std::string const& path)
{
    std::ifstream in(path);
    BOOST_REQUIRE_MESSAGE(in, "cannot open NUT bundle: " << path);
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    BOOST_REQUIRE(Json::parseFromStream(builder, in, &root, &errs));
    BOOST_REQUIRE_EQUAL(root["metadata"]["version"].asString(), "1.0.0");
    const auto& txs = root["transactions"];
    NutBundle bundle;
    bundle.deposits.reserve(txs.size());
    bundle.qualifiedIntents.reserve(txs.size());
    for (Json::ArrayIndex i = 0; i < txs.size(); ++i)
    {
        const auto& tx = txs[i];
        BOOST_REQUIRE(tx.isMember("intent"));
        auto qualified = karstNutQualifiedIntent(i, tx["intent"].asString());
        bundle.qualifiedIntents.push_back(qualified);
        DepositTx dep{.source_hash = upgradeDepositSourceHash(qualified),
            .from = parseAddress(tx["from"].asString()),
            .to = tx.isMember("to") && !tx["to"].isNull() ?
                      std::optional{parseAddress(tx["to"].asString())} :
                      std::nullopt,
            .mint = std::nullopt,
            .value = intx::uint256{0},
            .gas_limit = static_cast<int64_t>(tx["gasLimit"].asUInt64()),
            .is_system_tx = false,
            .data = parseData(tx["data"].asString())};
        bundle.upgradeGas += tx["gasLimit"].asUInt64();
        bundle.deposits.push_back(std::move(dep));
    }
    return bundle;
}

OpBlockTx depositBlockTx(DepositTx dep)
{
    auto const env = encodeDepositEnvelope(dep);
    return OpBlockTx{std::move(dep), evmc::bytes(env.begin(), env.end())};
}

OpBlockTx attributesDeposit()
{
    DepositTx dep{.source_hash = evmc::bytes32{},
        .from = OP_DEPOSITOR,
        .to = OP_L1_BLOCK,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 1'000'000,
        .is_system_tx = false,
        .data = evmc::bytes(JovianL1AttributesLen, 0)};
    std::copy(
        JovianL1AttributesSelector.begin(), JovianL1AttributesSelector.end(), dep.data.begin());
    return depositBlockTx(std::move(dep));
}

OpBlockTx userDeposit()
{
    DepositTx dep{
        .source_hash = 0x0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20_bytes32,
        .from = kSender,
        .to = kSender,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100'000,
        .is_system_tx = false,
        .data = {}};
    return depositBlockTx(std::move(dep));
}

OpBlockTx nutDeposit(DepositTx const& dep)
{
    return depositBlockTx(dep);
}

evmone::state::BlockInfo makeBlk(int64_t gasLimit)
{
    evmone::state::BlockInfo b;
    b.number = 100;
    b.gas_limit = gasLimit;
    b.base_fee = 7;
    return b;
}

evmone::state::Transaction makeOrdinaryTx(int64_t gasLimit)
{
    evmone::state::Transaction tx;
    tx.type = evmone::state::Transaction::Type::eip1559;
    tx.sender = kSender;
    tx.gas_limit = gasLimit;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;
    tx.to = 0x0000000000000000000000000000000000001234_address;
    return tx;
}

bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    static auto factory = [] {
        auto suite =
            std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
                std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
        return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(suite);
    }();
    return factory;
}

std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeSyntheticHeader(uint64_t gasLimit)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(100);
    h->setTimestamp(3'000'000);  // Karst schedule fixture ms
    h->setGasLimit(bcos::u256(gasLimit));
    h->setGasUsed(bcos::u256(0));
    h->setBaseFee(bcos::u256(1'000'000'000));
    h->setCoinbase(bcos::Address{});
    h->setPrevRandao(bcos::h256{});
    h->setParentBeaconBlockRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    return h;
}

std::vector<std::string_view> intentViews(std::vector<std::string> const& intents)
{
    std::vector<std::string_view> out;
    out.reserve(intents.size());
    for (auto const& s : intents)
        out.push_back(s);
    return out;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpKarstActivationSuite)

BOOST_AUTO_TEST_CASE(PinnedFixtureSha256AndUpgradeGas)
{
    const auto path = std::string(OP_KARST_NUT_BUNDLE_PATH);
    BOOST_CHECK_EQUAL(sha256Hex(path), kPinnedBundleSha256);
    const auto bundle = loadKarstNutBundle(path);
    BOOST_CHECK_EQUAL(bundle.deposits.size(), 31U);
    BOOST_CHECK_EQUAL(bundle.upgradeGas, KarstPinnedUpgradeGas);
    BOOST_CHECK_EQUAL(bundle.upgradeGas, 55'370'657U);
}

BOOST_AUTO_TEST_CASE(ActivationBlockOrderingL1UserThen31Nuts)
{
    const auto bundle = loadKarstNutBundle(OP_KARST_NUT_BUNDLE_PATH);
    auto intents = intentViews(bundle.qualifiedIntents);
    std::vector<OpBlockTx> txs;
    txs.push_back(attributesDeposit());
    txs.push_back(userDeposit());
    for (auto const& dep : bundle.deposits)
        txs.push_back(nutDeposit(dep));
    BOOST_CHECK_NO_THROW(validateKarstActivationOrder(txs, intents));
    BOOST_CHECK(*classifyKarstActivationDeposit(std::get<DepositTx>(txs[0].tx), intents) ==
                KarstActivationSegment::L1Attributes);
    BOOST_CHECK(*classifyKarstActivationDeposit(std::get<DepositTx>(txs[1].tx), intents) ==
                KarstActivationSegment::UserDeposit);
    for (size_t i = 0; i < bundle.deposits.size(); ++i)
    {
        BOOST_CHECK(isUpgradeDeposit(bundle.deposits[i], bundle.qualifiedIntents[i]));
        BOOST_CHECK(*classifyKarstActivationDeposit(bundle.deposits[i], intents) ==
                    KarstActivationSegment::NutUpgrade);
    }
}

BOOST_AUTO_TEST_CASE(ActivationBlockOrderingL1Then31NutsNoUser)
{
    const auto bundle = loadKarstNutBundle(OP_KARST_NUT_BUNDLE_PATH);
    auto intents = intentViews(bundle.qualifiedIntents);
    std::vector<OpBlockTx> txs;
    txs.push_back(attributesDeposit());
    for (auto const& dep : bundle.deposits)
        txs.push_back(nutDeposit(dep));
    BOOST_CHECK_NO_THROW(validateKarstActivationOrder(txs, intents));
}

BOOST_AUTO_TEST_CASE(ActivationBlockRejectsNutBeforeUser)
{
    const auto bundle = loadKarstNutBundle(OP_KARST_NUT_BUNDLE_PATH);
    auto intents = intentViews(bundle.qualifiedIntents);
    std::vector<OpBlockTx> txs;
    txs.push_back(attributesDeposit());
    txs.push_back(nutDeposit(bundle.deposits[0]));
    txs.push_back(userDeposit());
    BOOST_CHECK_THROW(validateKarstActivationOrder(txs, intents), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(KarstOrdinaryTxRejectsGasOverEip7825Cap)
{
    evmone::test::TestState ts;
    ts[kSender] = {
        .nonce = 0, .balance = intx::uint256{1'000'000'000'000'000'000}, .storage = {}, .code = {}};
    auto tx = makeOrdinaryTx(16'777'217);  // 2^24 + 1
    const std::vector<uint8_t> env{0x02, 0x11};
    const auto result = opValidate(ts, makeBlk(kSteadyStateGasLimit), tx, {env.data(), env.size()},
        karstConfig(), OpFeeParams{}, static_cast<int64_t>(kSteadyStateGasLimit));
    BOOST_REQUIRE(std::holds_alternative<std::error_code>(result));
    BOOST_CHECK(std::get<std::error_code>(result) ==
                evmone::state::make_error_code(evmone::state::MAX_GAS_LIMIT_EXCEEDED));
}

BOOST_AUTO_TEST_CASE(KarstNutDepositAcceptsGasOverEip7825Cap)
{
    const int64_t overCap = static_cast<int64_t>(evmone::state::MAX_TX_GAS_LIMIT + 1);
    DepositTx dep{.source_hash = upgradeDepositSourceHash("Karst 0: synthetic over-cap probe"),
        .from = kSender,
        .to = kSender,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = overCap,
        .is_system_tx = false,
        .data = {}};

    auto vm = evmc::VM{evmc_create_evmone()};
    evmone::test::TestState ts;
    evmone::test::TestBlockHashes hashes;
    evmone::state::StateDiff diff;
    const auto receipt = runDeposit(ts, makeBlk(KarstPinnedUpgradeGas + kSteadyStateGasLimit),
        hashes, dep, karstConfig(), vm, 0x2105, overCap, makeReceiptFactory(), diff);
    BOOST_REQUIRE(receipt);
}

BOOST_AUTO_TEST_CASE(HeaderGasLimitPreservedActivationInflated)
{
    const uint64_t inflated = kSteadyStateGasLimit + KarstPinnedUpgradeGas;
    auto header = makeSyntheticHeader(inflated);
    const auto blk = bcos::evm::engine::detail::toBlockInfo(*header);
    BOOST_CHECK_EQUAL(static_cast<uint64_t>(blk.gas_limit), inflated);
    BOOST_CHECK_EQUAL(header->gasLimit(), bcos::u256(inflated));
}

BOOST_AUTO_TEST_CASE(HeaderGasLimitPreservedNextBlockSteadyState)
{
    auto header = makeSyntheticHeader(kSteadyStateGasLimit);
    const auto blk = bcos::evm::engine::detail::toBlockInfo(*header);
    BOOST_CHECK_EQUAL(static_cast<uint64_t>(blk.gas_limit), kSteadyStateGasLimit);
}

BOOST_AUTO_TEST_CASE(HeaderGasLimitPreservedKeepKarstInflatedNextBlock)
{
    // Synthetic KeepKarstUpgradeGas=true: EL must not rewrite the announced inflated limit.
    const uint64_t inflated = kSteadyStateGasLimit + KarstPinnedUpgradeGas;
    auto header = makeSyntheticHeader(inflated);
    const auto blk = bcos::evm::engine::detail::toBlockInfo(*header);
    BOOST_CHECK_EQUAL(static_cast<uint64_t>(blk.gas_limit), inflated);
}

BOOST_AUTO_TEST_SUITE_END()
