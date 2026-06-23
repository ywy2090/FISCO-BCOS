#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/specs-tests/EvidenceKind.h"
#include "bcos-evm/specs-tests/ExecutionPath.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::evm::reference_tests
{

struct PathProfile
{
    ExecutionPath path{};
    EvidenceKind evidenceKind{};
    std::vector<std::string> enabledCapabilityRows;
    std::optional<std::string> unsupportedReason;
};

struct ForkProfile
{
    std::string profileId;
    std::string canonicalName;
    std::vector<std::string> aliases;
    std::string upstreamForkName;
    bcos::evm_standard::RevisionConfig revision;
    std::vector<std::string> activatedEips;
    std::vector<PathProfile> pathProfiles;
};

class ForkProfileRegistry
{
public:
    static ForkProfileRegistry const& instance();

    std::optional<ForkProfile> findByProfileId(std::string_view id) const;
    std::optional<ForkProfile> findByUpstreamFork(std::string_view fork) const;

private:
    ForkProfileRegistry();

    std::vector<ForkProfile> m_profiles;
};

}  // namespace bcos::evm::reference_tests
