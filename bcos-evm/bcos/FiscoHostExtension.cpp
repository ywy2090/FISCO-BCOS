#include "bcos-evm/bcos/FiscoHostExtension.h"
#include <cstring>

namespace bcos::evm
{

FiscoHostExtension::FiscoHostExtension(bool enableBalanceTransfer,
    FiscoPrecompileCaller precompileCaller, CreateFrameEntryHandler createFrameEntryHandler)
  : m_enableBalanceTransfer(enableBalanceTransfer),
    m_precompileCaller(std::move(precompileCaller)),
    m_createFrameEntryHandler(std::move(createFrameEntryHandler))
{}

std::optional<evmc_result> FiscoHostExtension::callFiscoPrecompile(
    evmc_revision rev, const evmc_message& msg)
{
    if (!m_precompileCaller)
    {
        return std::nullopt;
    }

    const evmc_address zero{};
    auto const target = std::memcmp(msg.code_address.bytes, zero.bytes, sizeof(zero.bytes)) != 0 ?
                            msg.code_address :
                            msg.recipient;
    if (!isFiscoPrecompileAddress(target))
    {
        return std::nullopt;
    }

    return m_precompileCaller(rev, msg);
}

void FiscoHostExtension::onCreateFrameEntry(evmc_revision rev, const evmc_message& msg)
{
    if (m_createFrameEntryHandler)
    {
        m_createFrameEntryHandler(rev, msg);
    }
}

bool FiscoHostExtension::isFiscoPrecompileAddress(const evmc_address& address) noexcept
{
    constexpr uint64_t FISCO_PRECOMPILE_MIN = 0x1000;

    // Keep small-address semantics (same family as PrecompiledManager lookup).
    for (size_t i = 0; i < 12; ++i)
    {
        if (address.bytes[i] != 0)
        {
            return false;
        }
    }

    uint64_t value = 0;
    for (size_t i = 12; i < sizeof(address.bytes); ++i)
    {
        value = (value << 8U) | static_cast<uint64_t>(address.bytes[i]);
    }
    return value >= FISCO_PRECOMPILE_MIN;
}

}  // namespace bcos::evm
