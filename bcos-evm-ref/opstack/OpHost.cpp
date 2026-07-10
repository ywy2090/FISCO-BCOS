#include <bcos-evm-ref/opstack/OpHost.h>

#include <bcos-evm-ref/opstack/OpPrecompiles.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <test/state/precompiles_internal.hpp>

using namespace evmc::literals;

namespace bcos::evmref::opstack
{
namespace
{
[[nodiscard]] bool isCreateKind(evmc_call_kind kind) noexcept
{
    return kind == EVMC_CREATE || kind == EVMC_CREATE2;
}

evmc::Result executeGasOverridePrecompile(
    const evmc::address& addr, const evmc_message& msg, int64_t gas_cost)
{
    if (msg.gas < gas_cost)
        return evmc::Result{EVMC_OUT_OF_GAS, 0};

    const int64_t gas_left = msg.gas - gas_cost;

    // gas-override 分支目前唯一实现是 P256Verify；未来新增另一个 gas-override 地址时必须在
    // 此显式补分支——宁可在这里失败，也不要沉默地对陌生地址跑 P256Verify 逻辑。
    if (addr != kP256VerifyAddress)
    {
        assert(false && "unhandled gas-override precompile address");
        return evmc::Result{EVMC_INTERNAL_ERROR, gas_left};
    }

    constexpr size_t kMaxOutput = 32;
    std::array<uint8_t, kMaxOutput> output{};
    const auto exec = evmone::state::p256verify_execute(
        msg.input_data, msg.input_size, output.data(), output.size());

    if (exec.status_code == EVMC_SUCCESS && exec.output_size > 0)
        return evmc::Result{EVMC_SUCCESS, gas_left, 0, output.data(), exec.output_size};

    return evmc::Result{exec.status_code, gas_left};
}

/// 复刻母本 Host::execute_message 中 EVMC_CALL 的 journal_create / touch / value 转账。
void applyCallValueSemantics(evmone::state::State& state, const evmc_message& msg) noexcept
{
    const auto exists = state.find(msg.recipient) != nullptr;
    if (!exists)
        state.journal_create(msg.recipient, exists);

    if (evmc::is_zero(msg.value))
    {
        state.touch(msg.recipient);
        return;
    }

    auto& dst_acc = state.get_or_insert(msg.recipient);
    const auto value = intx::be::load<intx::uint256>(msg.value);
    state.journal_balance_change(msg.sender, state.get(msg.sender).balance);
    state.journal_balance_change(msg.recipient, dst_acc.balance);
    state.get(msg.sender).balance -= value;
    dst_acc.balance += value;
}
}  // namespace

evmc_tx_context OpHost::get_tx_context() const noexcept
{
    const auto base_fee =
        (m_rev >= EVMC_LONDON) ? intx::uint256{m_block.base_fee} : intx::uint256{0};

    intx::uint256 effective_gas_price;
    if (m_tx.max_gas_price == 0)
    {
        effective_gas_price = 0;
    }
    else
    {
        assert(m_tx.max_gas_price >= base_fee);
        const auto priority_gas_price =
            std::min(m_tx.max_priority_gas_price, m_tx.max_gas_price - base_fee);
        effective_gas_price = base_fee + priority_gas_price;
    }

    return evmc_tx_context{
        intx::be::store<evmc::uint256be>(effective_gas_price),
        m_tx.sender,
        m_block.coinbase,
        m_block.number,
        m_block.timestamp,
        m_block.gas_limit,
        m_block.prev_randao,
        intx::be::store<evmc::uint256be>(intx::uint256{m_chain_id}),
        intx::be::store<evmc::uint256be>(intx::uint256{m_block.base_fee}),
        intx::be::store<evmc::uint256be>(m_block.blob_base_fee.value_or(0)),
        m_tx.blob_hashes.data(),
        m_tx.blob_hashes.size(),
        nullptr,
        0,
    };
}

evmc::Result OpHost::call(const evmc_message& msg) noexcept
{
    if (m_overrides == nullptr || isCreateKind(msg.kind))
        return evmone::state::Host::call(msg);

    const auto* entry = m_overrides->find(msg.code_address);
    if (entry == nullptr)
        return evmone::state::Host::call(msg);

    // EIP-7702：经 delegation 命中 precompile 地址时执行空 code，不跑 precompile。
    if ((msg.flags & EVMC_DELEGATED) != 0)
        return evmone::state::Host::call(msg);

    if (entry->max_input_size > 0 && static_cast<size_t>(msg.input_size) > entry->max_input_size)
        return evmc::Result{EVMC_FAILURE, 0};

    // 限长-only（0x08 / BLS）：回落基类（含 value/checkpoint/precompile 派发）。
    if (entry->gas_cost_override < 0)
        return evmone::state::Host::call(msg);

    // gas-override（0x100）：母本 is_precompile(PRAGUE)=false，必须自派发。
    // 复刻 Host::call 的 checkpoint + execute_message 派发前语义（无 logs：P256 不 emit）。
    if (msg.depth == 0)
    {
        if (const auto* sender = m_state.find(msg.sender);
            sender != nullptr && sender->nonce == evmone::state::Account::NonceMax)
            return evmc::Result{EVMC_FAILURE, msg.gas};
    }

    const auto state_checkpoint = m_state.checkpoint();

    if (msg.kind == EVMC_CALL)
        applyCallValueSemantics(m_state, msg);

    auto result = executeGasOverridePrecompile(entry->addr, msg, entry->gas_cost_override);

    if (result.status_code != EVMC_SUCCESS)
    {
        static constexpr auto addr_03 = 0x03_address;
        auto* const acc_03 = m_state.find(addr_03);
        const auto is_03_touched = acc_03 != nullptr && acc_03->erase_if_empty;

        m_state.rollback(state_checkpoint);

        // 母本 0x03 quirk：对该地址的 touch 永不回滚。
        if (is_03_touched && m_rev >= EVMC_SPURIOUS_DRAGON)
            m_state.touch(addr_03);
    }
    return result;
}

evmc_access_status OpHost::access_account(const evmc::address& addr) noexcept
{
    // override 表内地址恒温：op-geth statedb.Prepare 预热全部活跃 precompile（Isthmus 含
    // 0x100，母本 is_precompile 对 0x100 门槛为 OSAKA）；early-return 同时避免母本
    // get_or_insert(erase_if_empty) 产生的幽灵空账户进入 state diff。
    if (m_overrides != nullptr && m_overrides->contains(addr))
        return EVMC_ACCESS_WARM;
    return evmone::state::Host::access_account(addr);
}
}  // namespace bcos::evmref::opstack
