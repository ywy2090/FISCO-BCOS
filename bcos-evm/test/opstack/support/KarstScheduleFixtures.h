#pragma once

#include <bcos-evm/opstack/OpForkSchedule.h>

namespace bcos::evm::opstack
{
/// Test-only Karst schedule. Production `parse("…:karst")` still throws until K3;
/// this names Karst via `TestBypass` so later suites can resolve Osaka semantics.
/// Lives in `opstack` (not a nested `::test`) to avoid colliding with `evmone::test`
/// under the unity-build `using namespace bcos::evm::opstack`.
inline OpForkSchedule karstOnly()
{
    return OpForkSchedule{{{OpFork::Isthmus, 0}, {OpFork::Jovian, 1}, {OpFork::Karst, 2}},
        OpForkSchedule::TestBypass{}};
}
}  // namespace bcos::evm::opstack
