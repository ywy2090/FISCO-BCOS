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
}
}  // namespace bcos::evmref::opstack
