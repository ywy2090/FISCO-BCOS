#include "bcos-evm/evm-reference-tests/ForkProfileRegistry.h"

#include <algorithm>

namespace bcos::evm::reference_tests
{
namespace
{

bcos::evm_standard::RevisionConfig makeReferenceRevisionConfig(evmc_revision revision)
{
    // Keep aligned with EthPolicy::computeRevisionConfig for reference-path GST runs.
    bcos::evm_standard::RevisionConfig cfg;
    cfg.revision = revision;
    cfg.warm_access = revision >= EVMC_BERLIN;
    cfg.eip1153 = revision >= EVMC_CANCUN;
    cfg.eip4844 = revision >= EVMC_CANCUN;
    cfg.eip5656 = revision >= EVMC_CANCUN;
    cfg.eip6780 = revision >= EVMC_CANCUN;
    cfg.eip2537 = revision >= EVMC_PRAGUE;
    cfg.eip7623 = revision >= EVMC_PRAGUE;
    cfg.eip7702 = revision >= EVMC_PRAGUE;
    cfg.eip7212 = revision >= EVMC_OSAKA;
    cfg.eip7823 = revision >= EVMC_OSAKA;
    cfg.calldata_floor_per_token = cfg.eip7623 ? 10 : 0;
    return cfg;
}

std::vector<std::string> activatedEipsFor(bcos::evm_standard::RevisionConfig const& revision)
{
    std::vector<std::string> eips;
    if (revision.warm_access)
    {
        eips.emplace_back("EIP-2929");
    }
    if (revision.eip1153)
    {
        eips.emplace_back("EIP-1153");
    }
    if (revision.eip4844)
    {
        eips.emplace_back("EIP-4844");
    }
    if (revision.eip5656)
    {
        eips.emplace_back("EIP-5656");
    }
    if (revision.eip6780)
    {
        eips.emplace_back("EIP-6780");
    }
    if (revision.eip2537)
    {
        eips.emplace_back("EIP-2537");
    }
    if (revision.eip7623)
    {
        eips.emplace_back("EIP-7623");
    }
    if (revision.eip7702)
    {
        eips.emplace_back("EIP-7702");
    }
    return eips;
}

PathProfile referenceParityProfile()
{
    PathProfile profile;
    profile.path = ExecutionPath::Reference;
    profile.evidenceKind = EvidenceKind::ReferenceParity;
    return profile;
}

ForkProfile makeCancunProfile()
{
    auto const revision = makeReferenceRevisionConfig(EVMC_CANCUN);
    ForkProfile profile;
    profile.profileId = "eth-cancun";
    profile.canonicalName = "Ethereum Cancun";
    profile.aliases = {"Cancun"};
    profile.upstreamForkName = "Cancun";
    profile.revision = revision;
    profile.activatedEips = activatedEipsFor(revision);
    profile.pathProfiles = {referenceParityProfile()};
    return profile;
}

ForkProfile makePragueProfile()
{
    auto const revision = makeReferenceRevisionConfig(EVMC_PRAGUE);
    ForkProfile profile;
    profile.profileId = "eth-prague";
    profile.canonicalName = "Ethereum Prague";
    profile.aliases = {"Prague"};
    profile.upstreamForkName = "Prague";
    profile.revision = revision;
    profile.activatedEips = activatedEipsFor(revision);
    profile.pathProfiles = {referenceParityProfile()};
    return profile;
}

bool matchesForkName(ForkProfile const& profile, std::string_view fork)
{
    if (profile.upstreamForkName == fork)
    {
        return true;
    }
    return std::any_of(profile.aliases.begin(), profile.aliases.end(),
        [&](std::string const& alias) { return alias == fork; });
}

}  // namespace

ForkProfileRegistry::ForkProfileRegistry()
{
    m_profiles.push_back(makeCancunProfile());
    m_profiles.push_back(makePragueProfile());
}

ForkProfileRegistry const& ForkProfileRegistry::instance()
{
    static ForkProfileRegistry const registry;
    return registry;
}

std::optional<ForkProfile> ForkProfileRegistry::findByProfileId(std::string_view id) const
{
    for (auto const& profile : m_profiles)
    {
        if (profile.profileId == id)
        {
            return profile;
        }
    }
    return std::nullopt;
}

std::optional<ForkProfile> ForkProfileRegistry::findByUpstreamFork(std::string_view fork) const
{
    for (auto const& profile : m_profiles)
    {
        if (matchesForkName(profile, fork))
        {
            return profile;
        }
    }
    return std::nullopt;
}

}  // namespace bcos::evm::reference_tests
