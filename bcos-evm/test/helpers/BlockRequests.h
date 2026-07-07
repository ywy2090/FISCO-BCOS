#pragma once

#include "bcos-evm/eth-eest-test/TestStateView.h"
#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/StateDiff.hpp"
#include "bcos-evm/eth/state/StateKeyHash.hpp"
#include "helpers/BlockSystemCalls.h"
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace bcos::evm::reference_tests
{

inline constexpr char const* kInvalidDepositEventLayout =
    "BlockException.INVALID_DEPOSIT_EVENT_LAYOUT";
inline constexpr char const* kSystemContractEmpty = "BlockException.SYSTEM_CONTRACT_EMPTY";
inline constexpr char const* kSystemContractCallFailed =
    "BlockException.SYSTEM_CONTRACT_CALL_FAILED";

/// Fixed gas budget for Prague block-end system calls (evmone execute_system_call).
inline constexpr int64_t kRequestsSystemCallGas = 30'000'000;

/// Block-end system calls must fail the block on any vm error. System calls skip ADR-015
/// normalization (isCall); check raw evmc status_code.
inline bool isRequestsSystemCallSuccess(EthMessageResult const& output) noexcept
{
    return output.exitKind == StateTransitionExitKind::Completed &&
           output.evmcResult.status_code == EVMC_SUCCESS;
}

/// EIP-6110 deposit contract (mainnet).
inline constexpr evmc_address kDepositContractAddress{0x00, 0x00, 0x00, 0x00, 0x21, 0x9a, 0xb5,
    0x40, 0x35, 0x6c, 0xbb, 0x83, 0x9c, 0xbe, 0x05, 0x30, 0x3d, 0x77, 0x05, 0xfa};

/// keccak256("DepositEvent(...)") — EIP-6110 deposit log topic0.
inline constexpr evmc_bytes32 kDepositEventTopic{0x64, 0x9b, 0xbc, 0x62, 0xd0, 0xe3, 0x13, 0x42,
    0xaf, 0xea, 0x4e, 0x5c, 0xd8, 0x2d, 0x40, 0x49, 0xe7, 0xe1, 0xee, 0x91, 0x2f, 0xc0, 0x88, 0x9a,
    0xa7, 0x90, 0x80, 0x3b, 0xe3, 0x90, 0x38, 0xc5};

/// EIP-7002 withdrawal-request predeploy.
inline constexpr evmc_address kWithdrawalRequestAddress{0x00, 0x00, 0x09, 0x61, 0xef, 0x48, 0x0e,
    0xb5, 0x5e, 0x80, 0xd1, 0x9a, 0xd8, 0x35, 0x79, 0xa6, 0x4c, 0x00, 0x70, 0x02};

/// EIP-7251 consolidation-request predeploy.
inline constexpr evmc_address kConsolidationRequestAddress{0x00, 0x00, 0xbb, 0xdd, 0xc7, 0xce, 0x48,
    0x86, 0x42, 0xfb, 0x57, 0x9f, 0x8b, 0x00, 0xf3, 0xa5, 0x90, 0x00, 0x72, 0x51};

inline std::optional<uint32_t> readAbiWordAsSize(bcos::bytes const& data, size_t pos)
{
    if (data.size() < pos + 32)
        return std::nullopt;
    bcos::bytes word(data.begin() + static_cast<std::ptrdiff_t>(pos),
        data.begin() + static_cast<std::ptrdiff_t>(pos + 32));
    auto const v = bcos::fromBigQuantity(bcos::toHex(word));
    if (v > std::numeric_limits<uint32_t>::max())
        return std::nullopt;
    return static_cast<uint32_t>(v);
}

inline constexpr uint32_t padToWords(uint32_t size) noexcept
{
    return ((size + 31U) / 32U) * 32U;
}

/// Collect EIP-6110 deposit requests from block receipts (evmone collect_deposit_requests).
template <typename ReceiptRange>
inline std::optional<bcos::bytes> collectDepositRequests(ReceiptRange const& receipts)
{
    bcos::bytes depositRequest;
    depositRequest.push_back(0x00);  // Requests::Type::deposit

    for (auto const& receipt : receipts)
    {
        for (auto const& log : receipt.logs)
        {
            if (!state::AddressEqual{}(log.address, kDepositContractAddress))
                continue;
            if (log.topics.empty() || !state::Bytes32Equal{}(log.topics[0], kDepositEventTopic))
                continue;
            if (log.data.size() != 576)
                return std::nullopt;

            static constexpr uint32_t WORD = 32;
            static constexpr uint32_t DATA_SECTION = WORD * 5;
            static constexpr uint32_t PUBKEY_OFFSET = DATA_SECTION;
            static constexpr uint32_t PUBKEY_SIZE = 48;
            static constexpr uint32_t WITHDRAWAL_OFFSET =
                PUBKEY_OFFSET + WORD + padToWords(PUBKEY_SIZE);
            static constexpr uint32_t WITHDRAWAL_SIZE = 32;
            static constexpr uint32_t AMOUNT_OFFSET =
                WITHDRAWAL_OFFSET + WORD + padToWords(WITHDRAWAL_SIZE);
            static constexpr uint32_t AMOUNT_SIZE = 8;
            static constexpr uint32_t SIGNATURE_OFFSET =
                AMOUNT_OFFSET + WORD + padToWords(AMOUNT_SIZE);
            static constexpr uint32_t SIGNATURE_SIZE = 96;
            static constexpr uint32_t INDEX_OFFSET =
                SIGNATURE_OFFSET + WORD + padToWords(SIGNATURE_SIZE);
            static constexpr uint32_t INDEX_SIZE = 8;

            std::array<uint32_t, 5> offsets{};
            for (size_t i = 0; i < offsets.size(); ++i)
            {
                if (auto const w = readAbiWordAsSize(log.data, i * WORD))
                    offsets[i] = *w;
                else
                    return std::nullopt;
            }

            static constexpr std::array<uint32_t, 5> EXPECTED_OFFSETS{
                PUBKEY_OFFSET, WITHDRAWAL_OFFSET, AMOUNT_OFFSET, SIGNATURE_OFFSET, INDEX_OFFSET};
            if (offsets != EXPECTED_OFFSETS)
                return std::nullopt;

            auto const validateSizeAt = [&](uint32_t offset, uint32_t expected) {
                auto const size = readAbiWordAsSize(log.data, offset);
                return size.has_value() && *size == expected;
            };
            if (!validateSizeAt(PUBKEY_OFFSET, PUBKEY_SIZE) ||
                !validateSizeAt(WITHDRAWAL_OFFSET, WITHDRAWAL_SIZE) ||
                !validateSizeAt(AMOUNT_OFFSET, AMOUNT_SIZE) ||
                !validateSizeAt(SIGNATURE_OFFSET, SIGNATURE_SIZE) ||
                !validateSizeAt(INDEX_OFFSET, INDEX_SIZE))
                return std::nullopt;

            depositRequest.insert(depositRequest.end(), log.data.begin() + PUBKEY_OFFSET + WORD,
                log.data.begin() + PUBKEY_OFFSET + WORD + PUBKEY_SIZE);
            depositRequest.insert(depositRequest.end(), log.data.begin() + WITHDRAWAL_OFFSET + WORD,
                log.data.begin() + WITHDRAWAL_OFFSET + WORD + WITHDRAWAL_SIZE);
            depositRequest.insert(depositRequest.end(), log.data.begin() + AMOUNT_OFFSET + WORD,
                log.data.begin() + AMOUNT_OFFSET + WORD + AMOUNT_SIZE);
            depositRequest.insert(depositRequest.end(), log.data.begin() + SIGNATURE_OFFSET + WORD,
                log.data.begin() + SIGNATURE_OFFSET + WORD + SIGNATURE_SIZE);
            depositRequest.insert(depositRequest.end(), log.data.begin() + INDEX_OFFSET + WORD,
                log.data.begin() + INDEX_OFFSET + WORD + INDEX_SIZE);
        }
    }
    return depositRequest;
}

struct PragueRequestsCollectResult
{
    std::vector<bcos::bytes> requests;
    state::StateDiff stateDiff;
    std::optional<std::string> error;
};

/// Block-end Prague requests: EIP-6110 deposit logs + EIP-7002/7251 system calls.
template <typename ReceiptRange>
inline PragueRequestsCollectResult collectPragueBlockRequests(TestStateView& postTxState,
    state::BlockInfo const& blockInfo, bcos::evm::RevisionConfig const& revision, evmc::VM& vm,
    bcos::crypto::Hash& hashImpl, state::BlockHashes const& blockHashes,
    ReceiptRange const& receipts)
{
    PragueRequestsCollectResult out;

    if (auto depositBytes = collectDepositRequests(receipts))
        out.requests.push_back(std::move(*depositBytes));
    else
    {
        out.error = kInvalidDepositEventLayout;
        return out;
    }

    struct RequestsContract
    {
        evmc_address addr;
        uint8_t typeByte;
    };
    RequestsContract const contracts[] = {
        {kWithdrawalRequestAddress, 0x01},
        {kConsolidationRequestAddress, 0x02},
    };

    for (auto const& [addr, typeByte] : contracts)
    {
        auto const acc = postTxState.get_account(addr);
        if (!acc.has_value() || acc->code.empty())
        {
            out.error = kSystemContractEmpty;
            return out;
        }

        evmc_message msg{};
        msg.kind = EVMC_CALL;
        msg.depth = 0;
        msg.gas = kRequestsSystemCallGas;
        msg.sender = kSystemSenderAddress;
        msg.recipient = addr;
        msg.code_address = addr;

        EthMessageRequest input{};
        input.stateView = &postTxState;
        input.vm = &vm;
        input.hashImpl = &hashImpl;
        input.message = msg;
        input.blockInfo = blockInfo;
        input.revisionConfig = revision;
        input.isCall = true;
        if (blockHashes)
            input.blockHashes = blockHashes;
        else
            input.blockHashes = [](int64_t) { return evmc_bytes32{}; };

        auto output = task::syncWait(applyEthMessage(std::move(input)));
        if (!isRequestsSystemCallSuccess(output))
        {
            out.error = kSystemContractCallFailed;
            return out;
        }

        auto diff = output.stateDiff;
        diff.accounts.erase(kSystemSenderAddress);
        mergeStateDiffIntoView(postTxState, diff);
        for (auto const& [dAddr, dAcc] : diff.accounts)
            mergeStateDiffAccount(out.stateDiff.accounts[dAddr], dAcc);

        bcos::bytes request;
        request.push_back(typeByte);
        if (output.evmcResult.output_data != nullptr && output.evmcResult.output_size > 0)
        {
            request.insert(request.end(), output.evmcResult.output_data,
                output.evmcResult.output_data + output.evmcResult.output_size);
        }
        out.requests.push_back(std::move(request));
    }

    return out;
}

}  // namespace bcos::evm::reference_tests
