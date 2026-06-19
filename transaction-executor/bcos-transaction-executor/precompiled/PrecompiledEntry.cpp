#include "bcos-evm/bcos/PrecompiledEntry.h"
#include "bcos-executor/src/vm/Precompiled.h"

bcos::evm::Precompiled::Precompiled(decltype(m_precompiled) precompiled)
  : m_precompiled(std::move(precompiled))
{}

bcos::evm::Precompiled::Precompiled(
    decltype(m_precompiled) precompiled, ledger::Features::Flag flag)
  : m_precompiled(std::move(precompiled)), m_flag(flag)
{}

bcos::evm::Precompiled::Precompiled(decltype(m_precompiled) precompiled, size_t size)
  : m_precompiled(std::move(precompiled)), m_size(size)
{}

bcos::evm::Precompiled::~Precompiled() = default;

size_t bcos::evm::size(Precompiled const& precompiled)
{
    return precompiled.m_size;
}

std::optional<bcos::ledger::Features::Flag> bcos::evm::featureFlag(Precompiled const& precompiled)
{
    return precompiled.m_flag;
}
