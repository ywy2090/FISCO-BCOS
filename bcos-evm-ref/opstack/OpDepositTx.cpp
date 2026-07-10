#include <bcos-evm-ref/opstack/OpDepositTx.h>
#include <bcos-evm-ref/opstack/OpExecCommon.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpHost.h>
#include <cassert>
#include <limits>
#include <optional>
#include <stdexcept>
#include <test/state/bloom_filter.hpp>
#include <test/state/state.hpp>

namespace bcos::evmref::opstack
{
namespace
{
/// validate_transaction 的 view 面具：对 depositor 呈现「余额充足 + 空代码」，
/// 镜像 op-geth 对 deposit 跳过 preCheck 的 EOA/余额检查。value 可支付性由
/// runDeposit 内显式检查（对铸币后余额，op-geth state_transition.go:578 clause 6）。
class DepositValidationView final : public evmone::state::StateView
{
public:
    DepositValidationView(const evmone::state::StateView& base, const evmc::address& sender) noexcept
      : m_base{base}, m_sender{sender}
    {}

    std::optional<Account> get_account(const evmc::address& addr) const noexcept override
    {
        auto acc = m_base.get_account(addr);
        if (addr != m_sender)
            return acc;
        if (!acc.has_value())
            acc.emplace();
        acc->balance = std::numeric_limits<intx::uint256>::max();  // 屏蔽 INSUFFICIENT_FUNDS
        acc->code_hash = evmone::state::Account::EMPTY_CODE_HASH;  // 屏蔽 EIP-3607
        return acc;
    }

    evmone::state::bytes get_account_code(const evmc::address& addr) const noexcept override
    {
        return addr == m_sender ? evmone::state::bytes{} : m_base.get_account_code(addr);
    }

    evmone::state::bytes32 get_storage(
        const evmc::address& addr, const evmone::state::bytes32& key) const noexcept override
    {
        return m_base.get_storage(addr, key);
    }

private:
    const evmone::state::StateView& m_base;
    evmc::address m_sender;
};
}  // namespace
OpDepositReceipt runDeposit(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const DepositTx& dep, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId)
{
    if (dep.is_system_tx)
        throw std::runtime_error("op deposit: is_system_tx not supported (block error)");

    evmone::state::State state{view};
    auto& fromAcc = state.get_or_insert(dep.from);
    const uint64_t preNonce = fromAcc.nonce;
    if (dep.mint.has_value())
        fromAcc.balance += *dep.mint;

    evmone::state::Transaction tx;
    tx.type = evmone::state::Transaction::Type::legacy;  // 内部执行壳；receipt 用 kDepositTxType
    tx.sender = dep.from;
    tx.to = dep.to;
    tx.gas_limit = dep.gas_limit;
    tx.value = dep.value;
    tx.data = dep.data;
    tx.max_gas_price = 0;
    tx.max_priority_gas_price = 0;
    tx.nonce = preNonce;

    // Deposit 跳过 fee cap 校验；仅用 validate 求 intrinsic / EIP-7623 floor。
    evmone::state::BlockInfo validateBlock = block;
    validateBlock.base_fee = 0;
    const DepositValidationView maskedView{view, dep.from};
    const auto props = evmone::state::validate_transaction(
        maskedView, validateBlock, tx, cfg.rev, block.gas_limit, 0);

    evmone::state::TransactionReceipt receipt;
    receipt.type = kDepositTxType;

    if (std::holds_alternative<std::error_code>(props))
    {
        // 处理级失败（op-geth Regolith，state_transition.go:486-513）：
        // mint 保留、nonce 强制递增、gasUsed = gasLimit 全额（:498）。
        state.get(dep.from).nonce = preNonce + 1;
        receipt.status = EVMC_FAILURE;
        receipt.gas_used = dep.gas_limit;
    }
    else
    {
        const auto& p = std::get<evmone::state::TransactionProperties>(props);
        if (state.get(dep.from).balance < dep.value)
        {
            // op-geth clause 6（state_transition.go:578）：共识层错误 → failed-deposit
            // 分支（:486-513），gasUsed = gasLimit 全额（:498）。与 validate 失败同款。
            state.get(dep.from).nonce = preNonce + 1;
            receipt.status = EVMC_FAILURE;
            receipt.gas_used = dep.gas_limit;
        }
        else
        {
            // Host::prepare_message 对 depth==0 消息不自行 bump nonce（母本假定调用方已
            // bump，CREATE 地址派生用 nonce-1 取"执行前" nonce）——保留 2327532 的修复。
            assert(fromAcc.nonce < evmone::state::Account::NonceMax);
            ++fromAcc.nonce;
            OpHost host{cfg.rev, vm, state, block, hashes, tx, chainId, cfg.precompiles};
            auto outcome = executeMessage(state, host, tx, cfg.rev, block.coinbase,
                p.execution_gas_limit, p.min_gas_cost, /*delegation_refund=*/0);
            receipt.status = outcome.result.status_code;
            receipt.gas_used = outcome.gas_used;
            receipt.logs = host.take_logs();
        }
    }
    receipt.logs_bloom_filter = evmone::state::compute_bloom_filter(receipt.logs);
    receipt.state_diff = state.build_diff(cfg.rev);
    return OpDepositReceipt{std::move(receipt), preNonce, 1};
}
}  // namespace bcos::evmref::opstack
