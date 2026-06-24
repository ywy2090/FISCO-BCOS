#include "OpStackEestAdapter.h"

#include "bcos-evm/eth/AccessList.h"
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/Web3TypedTxKind.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackForkSchedule.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <boost/property_tree/ptree.hpp>
#include <cstring>
#include <stdexcept>
#include <string>

namespace bcos::eest::opstack
{
namespace
{

namespace pt = boost::property_tree;

[[noreturn]] void throwAdapterError(std::string const& message)
{
    throw std::runtime_error("OpStackEestAdapter: " + message);
}

bcos::u256 parseQuantity(std::string const& value)
{
    if (value.empty())
    {
        return 0;
    }
    return bcos::fromBigQuantity(value);
}

uint64_t parseUint64(std::string const& value)
{
    if (value.starts_with("0x") || value.starts_with("0X"))
    {
        return static_cast<uint64_t>(parseQuantity(value));
    }
    return std::stoull(value);
}

bcos::bytes parseHexBytes(std::string const& value)
{
    if (value.empty() || value == "0x")
    {
        return {};
    }
    return bcos::fromHex(value);
}

evmc_bytes32 parseBytes32(std::string const& value)
{
    auto const bytes = parseHexBytes(value);
    evmc_bytes32 out{};
    if (bytes.size() > sizeof(out.bytes))
    {
        throwAdapterError("Invalid bytes32 literal: " + value);
    }
    if (!bytes.empty())
    {
        std::memcpy(out.bytes + sizeof(out.bytes) - bytes.size(), bytes.data(), bytes.size());
    }
    return out;
}

/// Parse the "env" section into blockInfo.
bcos::evm::state::BlockInfo parseEnv(pt::ptree const& envTree)
{
    bcos::evm::state::BlockInfo blockInfo;
    blockInfo.number = static_cast<int64_t>(parseUint64(envTree.get<std::string>("currentNumber")));
    blockInfo.timestamp =
        static_cast<int64_t>(parseUint64(envTree.get<std::string>("currentTimestamp")));
    blockInfo.gasLimit =
        static_cast<int64_t>(parseUint64(envTree.get<std::string>("currentGasLimit")));
    blockInfo.coinbase =
        bcos::evm::state::parseHexAddress(envTree.get<std::string>("currentCoinbase"));

    if (auto const baseFee = envTree.get_optional<std::string>("currentBaseFee"))
    {
        blockInfo.baseFee = parseQuantity(*baseFee);
    }

    // EEST fixtures may include currentBlobBaseFee as a direct U256
    if (auto const blobBaseFee = envTree.get_optional<std::string>("currentBlobBaseFee"))
    {
        blockInfo.blobBaseFee = parseQuantity(*blobBaseFee);
    }

    return blockInfo;
}

/// Parse the "pre" section and insert into TestEvmStateReader.
void parseAndInsertPreState(
    pt::ptree const& preTree, bcos::evm::reference_tests::TestEvmStateReader& stateView)
{
    for (auto const& [addressHex, accountNode] : preTree)
    {
        auto const address = bcos::evm::state::parseHexAddress(addressHex);
        bcos::evm::state::Account account;
        account.balance = parseQuantity(accountNode.get<std::string>("balance", "0x0"));
        account.nonce = parseUint64(accountNode.get<std::string>("nonce", "0x0"));
        account.code = parseHexBytes(accountNode.get<std::string>("code", "0x"));

        if (auto const storage = accountNode.get_child_optional("storage"))
        {
            for (auto const& [slotHex, valueNode] : *storage)
            {
                account.storage.emplace(
                    parseBytes32(slotHex), parseBytes32(valueNode.get_value<std::string>()));
            }
        }

        stateView.insertAccount(address, std::move(account));
    }
}

// TODO(Phase 2): parsePostState — parses EEST post-state into Account map for assertions.
// Currently unused because EEST fixtures store expected state via stateRoot, not per-account JSON.

/// Insert the L1Block predeploy with zero L1/operator fee scalars.
void insertL1BlockPredeploy(bcos::evm::state::BlockInfo const& blockInfo,
    bcos::evm::reference_tests::TestEvmStateReader& stateView)
{
    using bcos::evm::L1_BASE_FEE_SLOT;
    using bcos::evm::L1_BLOB_BASE_FEE_SLOT;
    using bcos::evm::L1_FEE_SCALARS_SLOT;
    using bcos::evm::OP_L1_BLOCK_PREDEPLOY;
    using bcos::evm::OPERATOR_FEE_PARAMS_SLOT;
    using bcos::evm::state::toEvmC;

    bcos::evm::state::Account l1BlockAccount;

    // Slot 1: L1 base fee (use env.currentBaseFee, or 1 gwei if missing)
    bcos::u256 l1BaseFee = blockInfo.baseFee;
    if (l1BaseFee == 0)
    {
        l1BaseFee = bcos::u256(1'000'000'000);  // 1 gwei
    }
    l1BlockAccount.storage[toEvmC(L1_BASE_FEE_SLOT)] = toEvmC(l1BaseFee);

    // Slot 3: L1 fee scalars — zero (no L1 cost, so EEST post-states match standard Ethereum)
    l1BlockAccount.storage[toEvmC(L1_FEE_SCALARS_SLOT)] = toEvmC(bcos::u256(0));

    // Slot 7: L1 blob base fee (use env.currentBlobBaseFee, or 1 if missing)
    bcos::u256 blobBaseFee = blockInfo.blobBaseFee;
    if (blobBaseFee == 0)
    {
        blobBaseFee = bcos::u256(1);
    }
    l1BlockAccount.storage[toEvmC(L1_BLOB_BASE_FEE_SLOT)] = toEvmC(blobBaseFee);

    // Slot 8: Operator fee params — zero (no operator fee)
    l1BlockAccount.storage[toEvmC(OPERATOR_FEE_PARAMS_SLOT)] = toEvmC(bcos::u256(0));

    stateView.insertAccount(OP_L1_BLOCK_PREDEPLOY, std::move(l1BlockAccount));
}

/// Parse EIP-7702 authorization list from transaction tree.
std::vector<bcos::evm::SetCodeAuthorization> parseAuthorizationList(pt::ptree const& txTree)
{
    std::vector<bcos::evm::SetCodeAuthorization> authorizations;
    auto const authNode = txTree.get_child_optional("authorizationList");
    if (!authNode.has_value())
    {
        return authorizations;
    }

    for (auto const& [key, entryNode] : *authNode)
    {
        static_cast<void>(key);
        bcos::evm::SetCodeAuthorization entry;
        entry.chainId = parseQuantity(entryNode.get<std::string>("chainId", "0x0"));
        entry.address = bcos::evm::state::parseHexAddress(entryNode.get<std::string>("address"));
        entry.nonce = parseUint64(entryNode.get<std::string>("nonce", "0x0"));
        if (auto const yParity = entryNode.get_optional<std::string>("yParity"))
        {
            entry.yParity = parseUint64(*yParity);
        }
        else if (auto const v = entryNode.get_optional<std::string>("v"))
        {
            entry.yParity = parseUint64(*v);
        }
        if (auto const r = entryNode.get_optional<std::string>("r"))
        {
            entry.signatureR = parseHexBytes(*r);
        }
        if (auto const s = entryNode.get_optional<std::string>("s"))
        {
            entry.signatureS = parseHexBytes(*s);
        }
        if (auto const signer = entryNode.get_optional<std::string>("signer"))
        {
            entry.authority = bcos::evm::state::parseHexAddress(*signer);
        }
        authorizations.push_back(std::move(entry));
    }
    return authorizations;
}

/// Parse EIP-2930 access list from transaction tree (first variant only for state tests).
bcos::evm::Eip2930AccessList parseAccessList(pt::ptree const& txTree)
{
    using bcos::h160;
    using bcos::h256;
    using bcos::evm::state::fromEvmC;

    bcos::evm::Eip2930AccessList accessList;
    auto const listsNode = txTree.get_child_optional("accessLists");
    if (!listsNode.has_value() || listsNode->empty())
    {
        return accessList;
    }

    // State tests use the first access list variant.
    auto const& firstList = listsNode->begin()->second;
    for (auto const& [entryKey, entryNode] : firstList)
    {
        static_cast<void>(entryKey);
        h160 account;
        auto const addrStr = entryNode.get<std::string>("address");
        auto const parsedAddr = bcos::evm::state::parseHexAddress(addrStr);
        std::memcpy(account.data(), parsedAddr.bytes, sizeof(parsedAddr.bytes));

        std::vector<h256> keys;
        if (auto const storage = entryNode.get_child_optional("storageKeys"))
        {
            for (auto const& [slotKey, slotNode] : *storage)
            {
                static_cast<void>(slotKey);
                keys.emplace_back(fromEvmC(parseBytes32(slotNode.get_value<std::string>())));
            }
        }
        accessList.emplace_back(account, std::move(keys));
    }
    return accessList;
}

/// Parse blob versioned hashes from transaction tree.
std::vector<bcos::h256> parseBlobVersionedHashes(pt::ptree const& txTree)
{
    using bcos::h256;
    using bcos::evm::state::fromEvmC;

    std::vector<h256> hashes;
    auto const blobHashes = txTree.get_child_optional("blobVersionedHashes");
    if (!blobHashes.has_value())
    {
        return hashes;
    }

    for (auto const& [key, hashNode] : *blobHashes)
    {
        static_cast<void>(key);
        hashes.emplace_back(fromEvmC(parseBytes32(hashNode.get_value<std::string>())));
    }
    return hashes;
}

}  // namespace

OpStackEestFixture adaptStateFixture(pt::ptree const& fixtureJson, std::string const& forkName,
    evmc::VM& vm, bcos::crypto::Hash& hashImpl)
{
    OpStackEestFixture fixture;
    fixture.forkName = forkName;

    // 1. Parse env → blockInfo
    auto blockInfo = parseEnv(fixtureJson.get_child("env"));

    // Parse chainId from config section (needed for CHAINID opcode tests)
    if (auto const configNode = fixtureJson.get_child_optional("config"))
    {
        if (auto const chainIdValue = configNode->get_optional<std::string>("chainid"))
        {
            blockInfo.chainId = parseQuantity(*chainIdValue);
        }
    }

    // 2. Parse pre → TestEvmStateReader
    parseAndInsertPreState(fixtureJson.get_child("pre"), fixture.stateView);

    // 3. Insert L1Block predeploy with zero fees
    insertL1BlockPredeploy(blockInfo, fixture.stateView);

    // 4. Parse transaction → message + fields
    auto const& txTree = fixtureJson.get_child("transaction");

    // Determine if this is a CREATE or CALL
    auto const toStr = txTree.get_optional<std::string>("to");
    bool const isCreate = !toStr.has_value() || toStr->empty() || *toStr == "0x" || *toStr == "";

    // Parse nonce
    uint64_t nonce = parseUint64(txTree.get<std::string>("nonce", "0x0"));

    // Parse gas limit (first variant for state tests)
    uint64_t gasLimit = 0;
    auto const gasLimitNode = txTree.get_child_optional("gasLimit");
    if (gasLimitNode.has_value() && !gasLimitNode->empty())
    {
        auto const& firstGas = gasLimitNode->begin()->second;
        gasLimit = parseUint64(firstGas.get_value<std::string>());
    }

    // Parse value (first variant)
    bcos::u256 value{0};
    auto const valueNode = txTree.get_child_optional("value");
    if (valueNode.has_value() && !valueNode->empty())
    {
        auto const& firstValue = valueNode->begin()->second;
        value = parseQuantity(firstValue.get_value<std::string>());
    }

    // Parse data (first variant)
    bcos::bytes data;
    auto const dataNode = txTree.get_child_optional("data");
    if (dataNode.has_value() && !dataNode->empty())
    {
        auto const& firstData = dataNode->begin()->second;
        data = parseHexBytes(firstData.get_value<std::string>());
    }

    // Parse sender
    auto const senderStr = txTree.get_optional<std::string>("sender");
    evmc_address sender{};
    if (senderStr.has_value())
    {
        sender = bcos::evm::state::parseHexAddress(*senderStr);
    }

    // Parse fee caps
    bcos::u256 gasTipCap{0};
    bcos::u256 gasFeeCap{0};
    if (auto const maxFee = txTree.get_optional<std::string>("maxFeePerGas"))
    {
        gasFeeCap = parseQuantity(*maxFee);
    }
    if (auto const maxPriority = txTree.get_optional<std::string>("maxPriorityFeePerGas"))
    {
        gasTipCap = parseQuantity(*maxPriority);
    }
    if (auto const gasPrice = txTree.get_optional<std::string>("gasPrice"))
    {
        auto const parsedGasPrice = parseQuantity(*gasPrice);
        if (gasFeeCap == 0)
        {
            gasFeeCap = parsedGasPrice;
        }
        if (gasTipCap == 0)
        {
            gasTipCap = parsedGasPrice;
        }
    }

    // Parse blob gas fee cap
    bcos::u256 blobGasFeeCap{0};
    if (auto const maxBlobFee = txTree.get_optional<std::string>("maxFeePerBlobGas"))
    {
        blobGasFeeCap = parseQuantity(*maxBlobFee);
    }

    // Parse access list, authorizations, blob hashes
    auto accessList = parseAccessList(txTree);
    auto authorizations = parseAuthorizationList(txTree);
    auto blobVersionedHashes = parseBlobVersionedHashes(txTree);
    bool const authorizationListKeyPresent =
        txTree.get_child_optional("authorizationList").has_value();

    // Infer typed tx kind
    uint8_t web3TypedTxKind = bcos::evm::inferWeb3TypedTxKindFromFields(authorizationListKeyPresent,
        !authorizations.empty(), !blobVersionedHashes.empty(), gasFeeCap != 0 || gasTipCap != 0,
        !accessList.empty());

    // Build evmc_message
    evmc_message msg{};
    msg.kind = isCreate ? EVMC_CREATE : EVMC_CALL;
    msg.depth = 0;
    msg.gas = static_cast<int64_t>(gasLimit);
    msg.sender = sender;
    if (!isCreate && toStr.has_value())
    {
        auto const recipient = bcos::evm::state::parseHexAddress(*toStr);
        msg.recipient = recipient;
        msg.code_address = recipient;
    }
    msg.input_data = data.data();
    msg.input_size = data.size();
    msg.value = bcos::evm::state::toEvmC(value);

    // 5. Build OpStackExecutionRequest
    auto& input = fixture.input;
    input.stateView = &fixture.stateView;
    input.vm = &vm;
    input.hashImpl = &hashImpl;
    input.message = msg;
    input.nonce = nonce;
    input.gasTipCap = gasTipCap;
    input.gasFeeCap = gasFeeCap;
    input.blobGasFeeCap = blobGasFeeCap;
    input.blobVersionedHashes = std::move(blobVersionedHashes);
    input.blockInfo = std::move(blockInfo);
    input.revisionConfig = revisionConfigFromForkName(forkName);
    input.web3TypedTxKind = web3TypedTxKind;
    input.authorizationListPresent = authorizationListKeyPresent;
    input.authorizations = std::move(authorizations);
    // Zero-fee setup: all fees go to coinbase only (matching EIP-1559)
    input.forkSchedule = bcos::evm::makeIsthmusPlusForkSchedule();
    input.skipNonceChecks = false;
    input.skipTransactionChecks = false;
    input.noBaseFee = false;
    input.call = false;
    // EEST mode: for pre-London forks, zero baseFee so effectiveTip = full gasPrice.
    // OPStack refundGas always applies baseFee logic; without this, pre-London coinbase
    // only gets tip (effectiveGasPrice - baseFee) instead of full gasPrice.
    // For London+ forks, baseFee stays as-is — OPStack routes it to OP_BASE_FEE_RECIPIENT
    // (op-geth compatible), which EEST doesn't track but doesn't affect coinbase assertions.
    if (!input.revisionConfig.eip1559)
    {
        input.blockInfo.baseFee = 0;
    }
    // Store access list in fixture and wire pointer for OPStack intrinsic gas + warm-up.
    fixture.storedAccessList = std::move(accessList);
    if (!fixture.storedAccessList.empty())
    {
        input.accessList = &fixture.storedAccessList;
    }

    // 6. Parse post → expectedPost and expectSuccess
    fixture.expectSuccess = true;
    auto const postNode = fixtureJson.get_child_optional("post");
    if (postNode.has_value())
    {
        // EEST fixtures have post keyed by fork name, e.g. "Cancun", "Prague"
        // Each fork value is an array of expected states.
        // For state tests, EEST post contains "hash" (stateRoot) and "logs" (logsHash),
        // NOT per-account states. The account states are verified via state root comparison.
        // For OPStack we don't have stateRoot, so we'll compare via stateDiff.
        // Find the matching fork and use the first entry for expectException.
        for (auto const& [postForkName, postEntries] : *postNode)
        {
            if (postForkName.find(forkName) != std::string::npos ||
                forkName.find(postForkName) != std::string::npos)
            {
                if (!postEntries.empty())
                {
                    auto const& firstPost = postEntries.begin()->second;
                    if (auto const expectException =
                            firstPost.get_optional<std::string>("expectException"))
                    {
                        fixture.expectSuccess = false;
                    }
                }
                break;
            }
        }
    }

    // Set fixture name from forkName (caller can override)
    fixture.name = forkName;

    return fixture;
}

evmc_revision revisionFromForkName(std::string const& forkName)
{
    // Map common EEST fork names to evmc_revision.
    if (forkName == "Frontier")
        return EVMC_FRONTIER;
    if (forkName == "Homestead")
        return EVMC_HOMESTEAD;
    if (forkName == "EIP150" || forkName == "TangerineWhistle")
        return EVMC_TANGERINE_WHISTLE;
    if (forkName == "EIP158" || forkName == "SpuriousDragon")
        return EVMC_SPURIOUS_DRAGON;
    if (forkName == "Byzantium")
        return EVMC_BYZANTIUM;
    if (forkName == "Constantinople")
        return EVMC_CONSTANTINOPLE;
    if (forkName == "ConstantinopleFix" || forkName == "Petersburg")
        return EVMC_PETERSBURG;
    if (forkName == "Istanbul")
        return EVMC_ISTANBUL;
    if (forkName == "Berlin")
        return EVMC_BERLIN;
    if (forkName == "London")
        return EVMC_LONDON;
    if (forkName == "Paris" || forkName == "Merge")
        return EVMC_PARIS;
    if (forkName == "Shanghai")
        return EVMC_SHANGHAI;
    if (forkName == "Cancun")
        return EVMC_CANCUN;
    if (forkName == "Prague")
        return EVMC_PRAGUE;
    if (forkName == "Osaka")
        return EVMC_OSAKA;

    // Partial matches (fork names like "Cancun+4788")
    if (forkName.find("Cancun") != std::string::npos)
        return EVMC_CANCUN;
    if (forkName.find("Prague") != std::string::npos)
        return EVMC_PRAGUE;
    if (forkName.find("Osaka") != std::string::npos)
        return EVMC_OSAKA;
    if (forkName.find("Shanghai") != std::string::npos)
        return EVMC_SHANGHAI;

    // Default to latest
    return EVMC_OSAKA;
}

bcos::evm_standard::RevisionConfig revisionConfigFromForkName(std::string const& forkName)
{
    return bcos::evm_standard::revisionConfigFromRevision(revisionFromForkName(forkName));
}

}  // namespace bcos::eest::opstack
