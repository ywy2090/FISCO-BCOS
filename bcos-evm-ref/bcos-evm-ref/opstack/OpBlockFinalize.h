#pragma once

#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <test/state/state.hpp>

namespace bcos::evmref::opstack
{
/// OP 区块收尾：withdrawals 恒空、无 ommers/块奖励；EIP-6110/7002/7251 requests 按
/// cfg.disable_prague_requests 抑制——op-geth 对 OP Isthmus 显式禁用
/// （state_processor.go:140-156），全部 OP fork 该开关恒为 true；false 抛
/// std::invalid_argument（配置护栏）。
/// 范围注记：op-geth 在 OP Isthmus 仍执行 EIP-4788/2935 **执行前**系统调用
/// （state_processor.go:90-95）——那是块级编排（§4.4）接入时的前置步骤，不在本收尾函数内。
evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase);
}  // namespace bcos::evmref::opstack
