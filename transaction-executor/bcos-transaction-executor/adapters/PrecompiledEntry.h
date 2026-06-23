#pragma once

#include "bcos-evm/eth/precompiled/PrecompiledContract.h"
#include "bcos-framework/ledger/Features.h"
#include <memory>
#include <optional>
#include <variant>

namespace bcos::precompiled
{
class Precompiled;
}

namespace bcos::evm
{

struct Precompiled
{
    std::variant<PrecompiledContract, std::shared_ptr<precompiled::Precompiled>> m_precompiled;
    std::optional<ledger::Features::Flag> m_flag;
    size_t m_size{1};

    explicit Precompiled(decltype(m_precompiled) precompiled);
    Precompiled(decltype(m_precompiled) precompiled, ledger::Features::Flag flag);
    Precompiled(decltype(m_precompiled) precompiled, size_t size);
    ~Precompiled();
};

size_t size(Precompiled const& precompiled);
std::optional<ledger::Features::Flag> featureFlag(Precompiled const& precompiled);

}  // namespace bcos::evm
