#pragma once

#include "bcos-evm/eth/kernel/state-transition/DeductIntrinsicGas.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <evmc/evmc.h>
#include <atomic>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::evm::trace
{

struct EvmTraceContext
{
    std::string_view route{};
    std::optional<int64_t> blockNumber;
    std::optional<bcos::h256> txHash;
};

namespace detail
{
inline std::atomic<uint64_t>& traceSeq()
{
    static std::atomic<uint64_t> seq{1};
    return seq;
}

struct TraceFrame
{
    uint64_t id{0};
    std::string_view route{};
    std::optional<int64_t> blockNumber;
    std::optional<bcos::h256> txHash;
};

inline thread_local std::vector<TraceFrame> g_traceFrames;
}  // namespace detail

inline EvmTraceContext makeTraceContext(
    std::string_view route, int64_t blockNumber, std::optional<bcos::h256> txHash = std::nullopt)
{
    EvmTraceContext ctx;
    ctx.route = route;
    ctx.blockNumber = blockNumber;
    ctx.txHash = std::move(txHash);
    return ctx;
}

inline EvmTraceContext makeTraceContext(
    std::string_view route, std::optional<bcos::h256> txHash = std::nullopt)
{
    EvmTraceContext ctx;
    ctx.route = route;
    ctx.txHash = std::move(txHash);
    return ctx;
}

/// Returns the active trace id for the current thread, if any.
inline std::optional<uint64_t> currentTraceId() noexcept
{
    if (detail::g_traceFrames.empty())
    {
        return std::nullopt;
    }
    return detail::g_traceFrames.back().id;
}

/// Binds a monotonic trace id to the current thread for one top-level execution.
/// All EVM_LOG lines emitted while this object is alive share trace / block / txHash context.
class EvmTraceScope
{
public:
    explicit EvmTraceScope(EvmTraceContext ctx)
      : m_id(detail::traceSeq().fetch_add(1, std::memory_order_relaxed))
    {
        detail::g_traceFrames.push_back(
            detail::TraceFrame{m_id, ctx.route, ctx.blockNumber, ctx.txHash});
    }

    explicit EvmTraceScope(std::string_view route) : EvmTraceScope(makeTraceContext(route)) {}

    EvmTraceScope(EvmTraceScope const&) = delete;
    EvmTraceScope& operator=(EvmTraceScope const&) = delete;

    ~EvmTraceScope()
    {
        if (!detail::g_traceFrames.empty())
        {
            detail::g_traceFrames.pop_back();
        }
    }

    uint64_t id() const noexcept { return m_id; }

private:
    uint64_t m_id;
};

struct TraceLogPrefix
{
    template <typename Stream>
    friend Stream& operator<<(Stream& os, TraceLogPrefix const&)
    {
        if (detail::g_traceFrames.empty())
        {
            return os;
        }

        auto const& frame = detail::g_traceFrames.back();
        os << LOG_KV("trace", frame.id);
        if (!frame.route.empty())
        {
            os << LOG_KV("route", frame.route);
        }
        if (frame.blockNumber.has_value())
        {
            os << LOG_KV("block", *frame.blockNumber);
        }
        if (frame.txHash.has_value())
        {
            os << LOG_KV("txHash", frame.txHash->hexPrefixed());
        }
        return os;
    }
};

inline TraceLogPrefix traceLogPrefix() noexcept
{
    return {};
}

#define EVM_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("EVM") << bcos::evm::trace::traceLogPrefix()

inline std::string evmcAddress(evmc_address const& address)
{
    return toHex(std::span(address.bytes, sizeof(address.bytes)), "0x");
}

inline std::string_view callKind(evmc_call_kind kind) noexcept
{
    switch (kind)
    {
    case EVMC_CALL:
        return "CALL";
    case EVMC_DELEGATECALL:
        return "DELEGATECALL";
    case EVMC_CALLCODE:
        return "CALLCODE";
    case EVMC_CREATE:
        return "CREATE";
    case EVMC_CREATE2:
        return "CREATE2";
    default:
        return "UNKNOWN";
    }
}

inline std::string_view evmcStatus(evmc_status_code status) noexcept
{
    switch (status)
    {
    case EVMC_SUCCESS:
        return "SUCCESS";
    case EVMC_FAILURE:
        return "FAILURE";
    case EVMC_REVERT:
        return "REVERT";
    case EVMC_OUT_OF_GAS:
        return "OUT_OF_GAS";
    case EVMC_INVALID_INSTRUCTION:
        return "INVALID_INSTRUCTION";
    case EVMC_UNDEFINED_INSTRUCTION:
        return "UNDEFINED_INSTRUCTION";
    case EVMC_STACK_OVERFLOW:
        return "STACK_OVERFLOW";
    case EVMC_STACK_UNDERFLOW:
        return "STACK_UNDERFLOW";
    case EVMC_BAD_JUMP_DESTINATION:
        return "BAD_JUMP_DESTINATION";
    case EVMC_INVALID_MEMORY_ACCESS:
        return "INVALID_MEMORY_ACCESS";
    case EVMC_CALL_DEPTH_EXCEEDED:
        return "CALL_DEPTH_EXCEEDED";
    case EVMC_STATIC_MODE_VIOLATION:
        return "STATIC_MODE_VIOLATION";
    case EVMC_PRECOMPILE_FAILURE:
        return "PRECOMPILE_FAILURE";
    case EVMC_CONTRACT_VALIDATION_FAILURE:
        return "CONTRACT_VALIDATION_FAILURE";
    case EVMC_ARGUMENT_OUT_OF_RANGE:
        return "ARGUMENT_OUT_OF_RANGE";
    case EVMC_WASM_UNREACHABLE_INSTRUCTION:
        return "WASM_UNREACHABLE_INSTRUCTION";
    case EVMC_WASM_TRAP:
        return "WASM_TRAP";
    case EVMC_INSUFFICIENT_BALANCE:
        return "INSUFFICIENT_BALANCE";
    case EVMC_INTERNAL_ERROR:
        return "INTERNAL_ERROR";
    case EVMC_REJECTED:
        return "REJECTED";
    case EVMC_OUT_OF_MEMORY:
        return "OUT_OF_MEMORY";
    default:
        return "UNKNOWN";
    }
}

inline std::string_view exitKind(StateTransitionExitKind kind) noexcept
{
    switch (kind)
    {
    case StateTransitionExitKind::None:
        return "none";
    case StateTransitionExitKind::RulesRejected:
        return "rules_rejected";
    case StateTransitionExitKind::GasAffordRejected:
        return "gas_afford_rejected";
    case StateTransitionExitKind::IntrinsicRejected:
        return "intrinsic_rejected";
    case StateTransitionExitKind::Completed:
        return "completed";
    case StateTransitionExitKind::ExceptionHandled:
        return "exception_handled";
    default:
        return "unknown";
    }
}

inline std::string_view intrinsicGasMode(IntrinsicGasMode mode) noexcept
{
    switch (mode)
    {
    case IntrinsicGasMode::Skip:
        return "skip";
    case IntrinsicGasMode::AuthTuples:
        return "auth_tuples";
    case IntrinsicGasMode::FloorDataGas:
        return "floor_data_gas";
    case IntrinsicGasMode::OpStack:
        return "opstack";
    default:
        return "unknown";
    }
}

inline std::string_view intrinsicGasFailure(IntrinsicGasFailure failure) noexcept
{
    switch (failure)
    {
    case IntrinsicGasFailure::None:
        return "none";
    case IntrinsicGasFailure::GasLimitMinimum:
        return "gas_limit_minimum";
    case IntrinsicGasFailure::CalldataOutOfGas:
        return "calldata_out_of_gas";
    case IntrinsicGasFailure::AuthTupleOutOfGas:
        return "auth_tuple_out_of_gas";
    case IntrinsicGasFailure::OpStackIntrinsicOutOfGas:
        return "opstack_intrinsic_out_of_gas";
    default:
        return "unknown";
    }
}

inline void logMessageContext(evmc_message const& message)
{
    EVM_LOG(DEBUG) << LOG_DESC("tx message") << LOG_KV("kind", callKind(message.kind))
                   << LOG_KV("depth", message.depth) << LOG_KV("gas", message.gas)
                   << LOG_KV("sender", evmcAddress(message.sender))
                   << LOG_KV("recipient", evmcAddress(message.recipient))
                   << LOG_KV("code", evmcAddress(message.code_address))
                   << LOG_KV("inputSize", message.input_size);
}

}  // namespace bcos::evm::trace
