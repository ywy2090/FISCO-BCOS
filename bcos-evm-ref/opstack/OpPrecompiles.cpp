#include <bcos-evm-ref/opstack/OpPrecompiles.h>

namespace bcos::evmref::opstack
{
namespace
{
constexpr PrecompileOverrides::Entry kIsthmusEntries[] = {
    {.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 112687},
    {.addr = evmc::address{0x100}, .gas_cost_override = 3450, .max_input_size = 0},
    {.addr = evmc::address{0x0c}, .gas_cost_override = -1, .max_input_size = 513760},
    {.addr = evmc::address{0x0e}, .gas_cost_override = -1, .max_input_size = 488448},
    {.addr = evmc::address{0x0f}, .gas_cost_override = -1, .max_input_size = 235008},
};
}  // namespace

const PrecompileOverrides& isthmusPrecompileOverrides() noexcept
{
    static const PrecompileOverrides overrides{.entries = kIsthmusEntries};
    return overrides;
}
}  // namespace bcos::evmref::opstack
