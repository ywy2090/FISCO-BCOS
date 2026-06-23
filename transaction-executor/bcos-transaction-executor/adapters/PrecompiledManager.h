#pragma once

#include "PrecompiledEntry.h"
#include "bcos-evm/bcos/FiscoRevisionConfig.h"
#include "bcos-framework/ledger/Features.h"

namespace bcos::evm
{

class PrecompiledManager
{
public:
    explicit PrecompiledManager(crypto::Hash::Ptr hashImpl);
    Precompiled const* getPrecompiled(unsigned long contractAddress) const;
    Precompiled const* getPrecompiled(const evmc_address& address) const;

    // FIB-84: feature-aware lookup - returns nullptr if precompiled's flag is disabled
    Precompiled const* getPrecompiled(
        unsigned long contractAddress, const ledger::Features& features) const;
    Precompiled const* getPrecompiled(
        const evmc_address& address, const ledger::Features& features) const;

    // RevisionConfig+gated lookup - maps Flag->RevisionConfig for Ethereum EIP-gated
    // precompiles while retaining Features access for FISCO-native precompiles.
    Precompiled const* getPrecompiled(unsigned long contractAddress,
        const bcos::chain_policy::FiscoRevisionConfig& rev, const ledger::Features& features) const;

    Precompiled const* getPrecompiled(const evmc_address& address,
        const bcos::chain_policy::FiscoRevisionConfig& rev, const ledger::Features& features) const;

private:
    crypto::Hash::Ptr m_hashImpl;
    std::vector<std::tuple<unsigned long, Precompiled>> m_address2Precompiled{};
};

}  // namespace bcos::evm