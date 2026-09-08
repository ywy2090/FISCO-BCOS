#pragma once

// Test-only Karst schedule helpers. Production parse still cannot name `karst`;
// these use OpForkSchedule::TestBypass. Do not call from Initializer.

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <cstdint>
#include <memory>

namespace opstack_test
{
[[nodiscard]] std::shared_ptr<bcos::evm::opstack::OpForkSchedule> karstOnlySchedule(
    uint64_t karstTs);

[[nodiscard]] std::shared_ptr<bcos::evm::opstack::OpForkSchedule> isthmusThenJovian(
    uint64_t jovianTs);

[[nodiscard]] std::shared_ptr<bcos::evm::opstack::OpForkSchedule> legacySchedule(bool jovianActive);
}  // namespace opstack_test
