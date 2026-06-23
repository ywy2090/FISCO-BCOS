#pragma once

#include "bcos-evm/bcos/ports/AuthPort.h"
#include <functional>
#include <optional>

namespace bcos::evm::test
{

class InMemoryAuthAdapter final : public AuthPort
{
public:
    using CheckHandler = std::function<std::optional<EVMCResult>(evmc_message const&)>;
    using CreateHandler = std::function<void(evmc_message const&, std::string_view)>;

    explicit InMemoryAuthAdapter(CheckHandler check = {}, CreateHandler create = {})
      : m_check(std::move(check)), m_create(std::move(create))
    {}

    std::optional<EVMCResult> checkAuth(evmc_message const& msg) override
    {
        return m_check ? m_check(msg) : std::nullopt;
    }

    void createAuthTable(evmc_message const& msg, std::string_view tablePath) override
    {
        if (m_create)
        {
            m_create(msg, tablePath);
        }
    }

private:
    CheckHandler m_check;
    CreateHandler m_create;
};

}  // namespace bcos::evm::test
