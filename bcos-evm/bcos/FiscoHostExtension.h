#pragma once

#include "bcos-evm/eth/policy/HostExtension.h"
#include <evmc/evmc.h>
#include <functional>
#include <optional>

namespace bcos::evm
{

class FiscoHostExtension final : public state::HostExtension
{
public:
    using FiscoPrecompileCaller =
        std::function<std::optional<evmc_result>(evmc_revision, const evmc_message&)>;
    using CreateFrameEntryHandler = std::function<void(evmc_revision, const evmc_message&)>;

    explicit FiscoHostExtension(bool enableBalanceTransfer,
        FiscoPrecompileCaller precompileCaller = {},
        CreateFrameEntryHandler createFrameEntryHandler = {});

    bool allowSelfdestruct(const state::Account& /*unused*/) override { return false; }
    bool allowDelegateCallToPrecompile() override { return false; }
    bool skipHostValueTransfer() override { return m_enableBalanceTransfer; }

    std::optional<evmc_result> callFiscoPrecompile(
        evmc_revision rev, const evmc_message& msg) override;
    void onCreateFrameEntry(evmc_revision rev, const evmc_message& msg) override;

private:
    static bool isFiscoPrecompileAddress(const evmc_address& address) noexcept;

private:
    bool m_enableBalanceTransfer{false};
    FiscoPrecompileCaller m_precompileCaller;
    CreateFrameEntryHandler m_createFrameEntryHandler;
};

}  // namespace bcos::evm
