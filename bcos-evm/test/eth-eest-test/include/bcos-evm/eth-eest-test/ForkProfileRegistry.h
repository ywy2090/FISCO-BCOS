#pragma once

#include "bcos-evm/eth-eest-test/EvidenceKind.h"
#include "bcos-evm/eth-eest-test/ExecutionPath.h"
#include "bcos-evm/eth/RevisionConfig.h"
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
    bcos::evm::RevisionConfig revision;
    std::vector<std::string> activatedEips;
    std::vector<PathProfile> pathProfiles;
};

class ForkProfileRegistry
{
public:
    static ForkProfileRegistry const& instance();

    std::optional<ForkProfile> findByProfileId(std::string_view id) const;
    std::optional<ForkProfile> findByUpstreamFork(std::string_view fork) const;

    /// All registered profile ids (stable order).
    std::vector<std::string_view> allProfileIds() const;

    /// Map state_tests top-level dir segment (e.g. "cancun") → profile id.
    std::optional<std::string_view> profileIdForDirSegment(std::string_view segment) const;

    /// Apply postFork policy gates while keeping profile evmc_revision for VM execution.
    ForkProfile resolveExecutionProfile(
        ForkProfile profile, std::optional<std::string> const& postFork) const;

private:
    ForkProfileRegistry();

    std::vector<ForkProfile> m_profiles;
};

}  // namespace bcos::evm::reference_tests
