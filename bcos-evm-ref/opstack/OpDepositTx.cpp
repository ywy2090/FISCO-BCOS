#include <bcos-evm-ref/opstack/OpDepositTx.h>
#include <bcos-evm-ref/opstack/OpHost.h>
#include <algorithm>
#include <stdexcept>

namespace bcos::evmref::opstack
{
namespace
{
evmc_message build_deposit_message(
    const evmone::state::Transaction& tx, int64_t execution_gas_limit) noexcept
{
    const auto recipient = tx.to.has_value() ? *tx.to : evmc::address{};

    return {
        .kind = tx.to.has_value() ? EVMC_CALL : EVMC_CREATE,
        .flags = 0,
        .depth = 0,
        .gas = execution_gas_limit,
        .recipient = recipient,
        .sender = tx.sender,
        .input_data = tx.data.data(),
        .input_size = tx.data.size(),
        .value = intx::be::store<evmc::uint256be>(tx.value),
        .create2_salt = {},
        .code_address = recipient,
        .code = nullptr,
        .code_size = 0,
    };
}
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
    tx.type = evmone::state::Transaction::Type::legacy;
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
    const auto props = evmone::state::validate_transaction(
        view, validateBlock, tx, cfg.rev, block.gas_limit, 0);
    const auto snapshot = state.checkpoint();

    evmone::state::TransactionReceipt receipt;
    receipt.type = evmone::state::Transaction::Type::legacy;

    if (auto* err = std::get_if<std::error_code>(&props))
    {
        (void)err;
        state.rollback(snapshot);
        state.get(dep.from).nonce = preNonce + 1;
        receipt.status = EVMC_FAILURE;
        receipt.gas_used = dep.gas_limit;
    }
    else
    {
        const auto& p = std::get<evmone::state::TransactionProperties>(props);
        OpHost host{cfg.rev, vm, state, block, hashes, tx, chainId, cfg.precompiles};
        const auto result = host.call(build_deposit_message(tx, p.execution_gas_limit));
        const int64_t gasUsed =
            std::max<int64_t>(dep.gas_limit - result.gas_left, p.min_gas_cost);
        if (result.status_code != EVMC_SUCCESS)
        {
            state.rollback(snapshot);
            state.get(dep.from).nonce = preNonce + 1;
            receipt.status = result.status_code;
            receipt.gas_used = gasUsed;
            receipt.logs = host.take_logs();
        }
        else
        {
            state.get(dep.from).nonce = preNonce + 1;
            receipt.status = EVMC_SUCCESS;
            receipt.gas_used = gasUsed;
            receipt.logs = host.take_logs();
        }
    }
    receipt.state_diff = state.build_diff(cfg.rev);
    return OpDepositReceipt{std::move(receipt), preNonce, 1};
}
}  // namespace bcos::evmref::opstack
