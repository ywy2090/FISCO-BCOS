#pragma once
#include "PrecompiledEntry.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/kernel/EVMCResult.h"
#include "bcos-evm/eth/precompiled/EthBuiltinRegistry.h"
#include "bcos-evm/eth/precompiled/PrecompileTraits.h"
#include "bcos-executor/src/Common.h"
#include "bcos-executor/src/executive/BlockContext.h"
#include "bcos-executor/src/executive/TransactionExecutive.h"
#include "bcos-executor/src/vm/EvmPrecompiledAddress.h"
#include "bcos-executor/src/vm/Precompiled.h"
#include "transaction-executor/bcos-transaction-executor/ExecutiveWrapper.h"

// Forward-declared; defined in bcos-executor/src/vm/ModexpGas.h.
namespace bcos::evm
{
bcos::bigint calcModexpGas(bcos::bytesConstRef input, evmc_revision revision);
bool shouldRejectModexpEip7823(evmc_address const& addr, bcos::bytesConstRef input,
    const bcos::evm_standard::RevisionConfig& rev, evmc_revision revision) noexcept;
}  // namespace bcos::evm
#include "bcos-framework/ledger/Features.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-utilities/Overloaded.h"
#include <evmc/evmc.h>
#include <boost/exception/diagnostic_information.hpp>
#include <exception>
#include <limits>
#include <memory>
#include <range/v3/algorithm/copy.hpp>
#include <variant>

namespace bcos::evm
{

#define PRECOMPILE_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("PRECOMPILE")

// Build an EVMCResult for a built-in precompiled call, taking ownership of the output buffer.
inline EVMCResult buildBuiltinPrecompiledResult(bool success, auto const& output, int64_t gasLeft)
{
    auto buffer = std::make_unique_for_overwrite<uint8_t[]>(output.size());
    ::ranges::copy(output, buffer.get());
    return EVMCResult{
        evmc_result{
            .status_code = success ? EVMC_SUCCESS : EVMC_REVERT,
            .gas_left = gasLeft,
            .gas_refund = 0,
            .output_data = buffer.release(),
            .output_size = output.size(),
            .release = [](const struct evmc_result* result) { delete[] result->output_data; },
            .create_address = {},
            .padding = {},
        },
        success ? protocol::TransactionStatus::None :
                  protocol::TransactionStatus::RevertInstruction};
}

inline bcos::bigint builtinPrecompileGasCost(
    executor::PrecompiledContract const& precompiledContract, bytesConstRef input,
    evmc_message const& message, evmc_revision revision)
{
    (void)message;
    return precompiledContract.cost(input, revision);
}

// Execute an EVM built-in precompiled contract (sha256, ecrecover, etc.).
// ── New path: PrecompileTraits-based lookup (compile-time table) ──
inline EVMCResult callBuiltinPrecompiled(evmc_message const& message,
    const bcos::evm_standard::RevisionConfig& rev, evmc_revision revision, bool fixErrorHandling)
{
    bytesConstRef const input{message.input_data, message.input_size};
    const auto* traits = precompiles::findPrecompile(revision, message.recipient);
    if (!traits)
    {
        return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
            protocol::TransactionStatus::RevertInstruction, EVMC_FAILURE, 0, "Unknown precompile");
    }

    if (shouldRejectModexpEip7823(message.recipient, input, rev, revision))
    {
        return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
            protocol::TransactionStatus::RevertInstruction, EVMC_FAILURE, 0,
            "modexp EIP-7823 input limit exceeded");
    }

    // Gas computation from compile-time traits
    bigint gas;
    if (precompiles::hasRevisionAwarePricer(traits))
    {
        gas = builtinPricerBySuffix(traits->address_suffix)(input);
    }
    else
    {
        gas = traits->gas_base + traits->gas_per_word * ((input.size() + 31) / 32);
    }

    if (fixErrorHandling)
    {
        if (gas > std::numeric_limits<int64_t>::max() || gas < 0)
        {
            return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
                protocol::TransactionStatus::OutOfGas, EVMC_OUT_OF_GAS, 0,
                "Precompiled contract gas cost overflow", fixErrorHandling);
        }
        const auto gasCost = gas.template convert_to<int64_t>();
        if (gasCost > message.gas)
        {
            return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
                protocol::TransactionStatus::OutOfGas, EVMC_OUT_OF_GAS, 0,
                "Precompiled contract out of gas", fixErrorHandling);
        }
        auto [success, output] = builtinExecutorBySuffix(traits->address_suffix)(input);
        return buildBuiltinPrecompiledResult(success, output, message.gas - gasCost);
    }

    auto [success, output] = builtinExecutorBySuffix(traits->address_suffix)(input);
    return buildBuiltinPrecompiledResult(
        success, output, message.gas - gas.template convert_to<int64_t>());
}

// Execute a bcos precompiled contract (BFS, table ops, auth, etc.).
inline EVMCResult callBcosPrecompiled(
    std::shared_ptr<precompiled::Precompiled> const& precompiledContract, auto& storage,
    protocol::BlockHeader const& blockHeader, evmc_message const& message,
    evmc_address const& origin, ExternalCaller auto&& externalCaller,
    auto const& precompiledManager, int64_t contextID, int64_t seq, bool authCheck,
    bool fixErrorHandling)
{
    using namespace std::string_literals;
    auto contractAddress = address2HexString(message.code_address);
    auto executive = buildLegacyExecutive(storage, blockHeader, contractAddress,
        std::forward<decltype(externalCaller)>(externalCaller), precompiledManager, contextID, seq,
        authCheck);

    auto params = std::make_shared<precompiled::PrecompiledExecResult>();
    params->m_sender = address2HexString(message.sender);
    params->m_codeAddress = std::move(contractAddress);
    params->m_precompiledAddress = address2HexString(message.recipient);
    params->m_origin = address2HexString(origin);
    params->m_input = {message.input_data, message.input_size};
    params->m_gasLeft = message.gas;
    params->m_staticCall = (message.flags & EVMC_STATIC) != 0;
    params->m_create = (message.kind == EVMC_CREATE);

    // FIB-80: use remaining gas on revert (EVM-spec). Clamp to [0, message.gas] to defend
    // against buggy precompiled implementations.
    auto errorGas = [&] {
        return fixErrorHandling ?
                   std::clamp(params->m_gasLeft, static_cast<int64_t>(0), message.gas) :
                   message.gas;
    };

    try
    {
        auto response = precompiledContract->call(executive, params);

        auto buffer = std::make_unique<uint8_t[]>(params->m_execResult.size());
        std::uninitialized_copy(
            params->m_execResult.begin(), params->m_execResult.end(), buffer.get());
        return EVMCResult{
            evmc_result{
                .status_code = EVMC_SUCCESS,
                .gas_left = response->m_gasLeft,
                .gas_refund = 0,
                .output_data = buffer.release(),
                .output_size = params->m_execResult.size(),
                .release = [](const struct evmc_result* result) { delete[] result->output_data; },
                .create_address = {},
                .padding = {},
            },
            protocol::TransactionStatus::None};
    }
    catch (protocol::PrecompiledError const& e)
    {
        PRECOMPILE_LOG(WARNING) << "Revert transaction: PrecompiledFailed"
                                << LOG_KV("address", params->m_codeAddress)
                                << LOG_KV("message", e.what());
        return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
            protocol::TransactionStatus::PrecompiledError, EVMC_REVERT, errorGas(), e.what(),
            fixErrorHandling);
    }
    catch (std::exception& e)
    {
        PRECOMPILE_LOG(WARNING) << "Precompiled execute error: "
                                << boost::diagnostic_information(e);
        return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
            protocol::TransactionStatus::PrecompiledError, EVMC_REVERT, errorGas(),
            "InternalPrecompiledFailed"s, fixErrorHandling);
    }
}

inline constexpr struct
{
    // FIB-79: removed noexcept so the outer catch can return a receipt instead of std::terminate.
    // `rev` carries all hard-fork configuration flags so future additions don't require new params.
    EVMCResult operator()(Precompiled const& precompiled, auto& storage,
        protocol::BlockHeader const& blockHeader, evmc_message const& message,
        evmc_address const& origin, ExternalCaller auto&& externalCaller,
        auto const& precompiledManager, int64_t contextID, int64_t seq, bool authCheck,
        const bcos::evm_standard::RevisionConfig& rev, evmc_revision revision,
        bool fixErrorHandling) const
    {
        const bool bugfixPrecompiled = fixErrorHandling;

        try
        {
            return std::visit(
                bcos::overloaded{[&](PrecompiledContract const& /*contract*/) {
                                     return callBuiltinPrecompiled(
                                         message, rev, revision, fixErrorHandling);
                                 },
                    [&](std::shared_ptr<precompiled::Precompiled> const& contract) {
                        return callBcosPrecompiled(contract, storage, blockHeader, message, origin,
                            std::forward<decltype(externalCaller)>(externalCaller),
                            precompiledManager, contextID, seq, authCheck, fixErrorHandling);
                    }},
                precompiled.m_precompiled);
        }
        catch (std::exception& e)
        {
            PRECOMPILE_LOG(ERROR) << "Unexpected precompiled exception: " << e.what();
            // FIB-79: preserve old std::terminate behavior when flag off; return receipt when on.
            if (!bugfixPrecompiled)
            {
                throw;
            }
            return makeErrorEVMCResult(*executor::GlobalHashImpl::g_hashImpl,
                protocol::TransactionStatus::PrecompiledError, EVMC_INTERNAL_ERROR, 0,
                "InternalPrecompiledError", fixErrorHandling);
        }
    }
} callPrecompiled{};

}  // namespace bcos::evm
