#pragma once

#include "bcos-evm/eth/policy/HostExtension.h"
#include <cstring>

namespace bcos::evm
{
class OpHostExtension final : public state::HostExtension
{
public:
    static constexpr evmc_address OP_L1_BLOCK_PREDEPLOY = {
        .bytes = {0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15}};

    void prepareMessage(evmc_revision /*rev*/, const evmc_message& /*msg*/) override {}

    std::optional<evmc_result> tryChainPrecompile(
        evmc_revision /*rev*/, const evmc_message& msg) override
    {
        auto const target = msg.code_address;
        if (std::memcmp(target.bytes, OP_L1_BLOCK_PREDEPLOY.bytes, sizeof(target.bytes)) == 0)
        {
            // TODO(opstack): wire L1Block predeploy execution.
            return std::nullopt;
        }
        return std::nullopt;
    }
};
}  // namespace bcos::evm
