#include <bcos-evm-ref/opstack/OpForkSchedule.h>

namespace bcos::evmref::opstack
{
const OpForkConfig& isthmusConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Isthmus,
        .rev = EVMC_PRAGUE,
        .precompiles = nullptr,
        .disable_prague_requests = true,
        .has_operator_fee = true,
    };
    return cfg;
}
}  // namespace bcos::evmref::opstack
