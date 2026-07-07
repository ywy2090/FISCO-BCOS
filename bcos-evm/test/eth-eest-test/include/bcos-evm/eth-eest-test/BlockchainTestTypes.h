#pragma once

#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include "bcos-evm/eth-eest-test/TestStateView.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bcos::evm::reference_tests
{

/// GAS_PER_BLOB (EIP-4844): constant across forks.
inline constexpr uint64_t GAS_PER_BLOB = 1u << 17;  // 131072

/// Fork-dependent blob parameters (evmone: state::BlobParams).
struct BlobParams
{
    uint16_t target = 3;
    uint16_t max = 6;
    uint32_t baseFeeUpdateFraction = 3338477;
};

/// Keyed by EEST network name ("Cancun", "Prague", ...); evmone: BlobSchedule map.
using BlobSchedule = std::unordered_map<std::string, BlobParams>;

/// EIP-4895 withdrawal. `amount` is in Gwei.
struct Withdrawal
{
    uint64_t index = 0;
    uint64_t validatorIndex = 0;
    evmc_address address{};
    uint64_t amount = 0;
};

/// Block header as it appears in EEST fixtures (evmone: BlockHeader).
struct TestBlockHeader
{
    evmc_bytes32 parentHash{};
    evmc_address coinbase{};
    evmc_bytes32 stateRoot{};
    evmc_bytes32 receiptsRoot{};  // JSON: receiptTrie
    bcos::bytes logsBloom;        // 256 bytes; JSON: bloom
    int64_t difficulty = 0;
    evmc_bytes32 prevRandao{};  // JSON: mixHash
    int64_t blockNumber = 0;    // JSON: number
    int64_t gasLimit = 0;
    int64_t gasUsed = 0;
    int64_t timestamp = 0;
    bcos::bytes extraData;
    uint64_t baseFeePerGas = 0;
    evmc_bytes32 hash{};
    evmc_bytes32 transactionsRoot{};  // JSON: transactionsTrie

    // Shanghai+
    evmc_bytes32 withdrawalsRoot{};

    // Cancun+
    evmc_bytes32 parentBeaconBlockRoot{};
    std::optional<uint64_t> blobGasUsed;
    std::optional<uint64_t> excessBlobGas;

    // Prague+
    evmc_bytes32 requestsHash{};
    uint64_t slotNumber = 0;  // Osaka+; JSON: slotNumber
};

/// One block within a blockchain test (evmone: TestBlock).
struct TestBlock
{
    state::BlockInfo blockInfo;  // execution context (parentHash filled in)
    std::vector<GstTransactionTemplate> transactions;
    std::vector<bcos::bytes> rawTxRlp;           // signed canonical tx RLP (Task 2.1)
    std::vector<Withdrawal> withdrawals;         // Shanghai+
    std::optional<uint64_t> inputBlobGasUsed;    // Cancun+
    std::optional<uint64_t> inputExcessBlobGas;  // Cancun+
    size_t rlpSize = 0;                          // Osaka+ (EIP-7934)
    bool withdrawalsParseSuccess = true;
    bool hasOmmers = false;            // Paris+ rule #8
    bool hasStructuredHeader = false;  // false for rlp-only invalid blocks without rlp_decoded
    std::string expectException;       // empty => valid block
    TestBlockHeader expectedBlockHeader;
};

/// A full blockchain test (evmone: BlockchainTest).
struct BlockchainTest
{
    std::string name;
    std::string network;  // "Cancun", ...
    TestStateView preState;
    TestBlockHeader genesisBlockHeader;
    std::vector<TestBlock> testBlocks;
    BlobSchedule blobSchedule;  // from config.blobSchedule
    bcos::u256 chainId{0};      // from config.chainid
    evmc_bytes32 lastBlockHash{};
    /// postState account map (empty => compare via postStateHash instead).
    std::vector<std::pair<evmc_address, state::Account>> postState;
    std::optional<evmc_bytes32> postStateHash;
};

}  // namespace bcos::evm::reference_tests
