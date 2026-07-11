#include <bcos-evm-ref/opstack/OpPredeploys.h>
#include <test/utils/test_state.hpp>

namespace bcos::evmref::opstack
{
void seedOpPredeploys(evmone::test::TestState& state)
{
    for (const auto& addr : {OP_L1_BLOCK, OP_GAS_PRICE_ORACLE, OP_SEQUENCER_FEE_VAULT,
             OP_BASE_FEE_VAULT, OP_L1_FEE_VAULT, OP_OPERATOR_FEE_VAULT})
    {
        state[addr];  // 插入默认账户
    }
    // 四个 fee vault：最小非空 runtime code（1 字节 STOP），使其在零值差分下不被当空账户删除。
    for (const auto& v :
        {OP_SEQUENCER_FEE_VAULT, OP_BASE_FEE_VAULT, OP_L1_FEE_VAULT, OP_OPERATOR_FEE_VAULT})
    {
        state[v].code = evmc::bytes{0x00};
    }
}
}  // namespace bcos::evmref::opstack
