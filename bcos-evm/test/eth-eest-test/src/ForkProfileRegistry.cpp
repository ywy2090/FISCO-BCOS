#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "bcos-evm/eth/RevisionConfig.h"

#include <algorithm>

namespace bcos::evm::reference_tests
{
namespace
{

bcos::evm::RevisionConfig makeReferenceRevisionConfig(evmc_revision revision)
{
    return bcos::evm::revisionConfigFromRevision(revision);
}

std::vector<std::string> activatedEipsFor(bcos::evm::RevisionConfig const& revision)
{
    std::vector<std::string> eips;
    if (revision.eip2929)
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
    if (revision.eip7212)
    {
        eips.emplace_back("EIP-7212");
    }
    if (revision.eip7823)
    {
        eips.emplace_back("EIP-7823");
    }
    if (revision.eip7825)
    {
        eips.emplace_back("EIP-7825");
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

ForkProfile makeHomesteadProfile()
{
    auto const revision = makeReferenceRevisionConfig(EVMC_HOMESTEAD);
    ForkProfile profile;
    profile.profileId = "eth-homestead";
    profile.canonicalName = "Ethereum Homestead";
    profile.aliases = {"Homestead"};
    profile.upstreamForkName = "Homestead";
    profile.revision = revision;
    profile.activatedEips = activatedEipsFor(revision);
    profile.pathProfiles = {referenceParityProfile()};
    return profile;
}

ForkProfile makeBerlinProfile()
{
    auto const revision = makeReferenceRevisionConfig(EVMC_BERLIN);
    ForkProfile profile;
    profile.profileId = "eth-berlin";
    profile.canonicalName = "Ethereum Berlin";
    profile.aliases = {"Berlin"};
    profile.upstreamForkName = "Berlin";
    profile.revision = revision;
    profile.activatedEips = activatedEipsFor(revision);
    profile.pathProfiles = {referenceParityProfile()};
    return profile;
}

ForkProfile makeLondonProfile()
{
    auto const revision = makeReferenceRevisionConfig(EVMC_LONDON);
    ForkProfile profile;
    profile.profileId = "eth-london";
    profile.canonicalName = "Ethereum London";
    profile.aliases = {"London"};
    profile.upstreamForkName = "London";
    profile.revision = revision;
    profile.activatedEips = activatedEipsFor(revision);
    profile.pathProfiles = {referenceParityProfile()};
    return profile;
}

ForkProfile makeParisProfile()
{
    auto const revision = makeReferenceRevisionConfig(EVMC_PARIS);
    ForkProfile profile;
    profile.profileId = "eth-paris";
    profile.canonicalName = "Ethereum Paris (Merge)";
    profile.aliases = {"Paris", "Merge"};
    profile.upstreamForkName = "Paris";
    profile.revision = revision;
    profile.activatedEips = activatedEipsFor(revision);
    profile.pathProfiles = {referenceParityProfile()};
    return profile;
}

ForkProfile makeShanghaiProfile()
{
    auto const revision = makeReferenceRevisionConfig(EVMC_SHANGHAI);
    ForkProfile profile;
    profile.profileId = "eth-shanghai";
    profile.canonicalName = "Ethereum Shanghai";
    profile.aliases = {"Shanghai"};
    profile.upstreamForkName = "Shanghai";
    profile.revision = revision;
    profile.activatedEips = activatedEipsFor(revision);
    profile.pathProfiles = {referenceParityProfile()};
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

ForkProfile makeOsakaProfile()
{
    auto const revision = makeReferenceRevisionConfig(EVMC_OSAKA);
    ForkProfile profile;
    profile.profileId = "eth-osaka";
    profile.canonicalName = "Ethereum Osaka";
    profile.aliases = {"Osaka"};
    profile.upstreamForkName = "Osaka";
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

std::optional<std::string_view> upstreamForkFromDirSegment(std::string_view segment)
{
    if (segment == "homestead")
    {
        return "Homestead";
    }
    if (segment == "berlin")
    {
        return "Berlin";
    }
    if (segment == "london")
    {
        return "London";
    }
    if (segment == "paris" || segment == "merge")
    {
        return "Paris";
    }
    if (segment == "shanghai")
    {
        return "Shanghai";
    }
    if (segment == "cancun")
    {
        return "Cancun";
    }
    if (segment == "prague")
    {
        return "Prague";
    }
    if (segment == "osaka")
    {
        return "Osaka";
    }
    return std::nullopt;
}

}  // namespace

ForkProfileRegistry::ForkProfileRegistry()
{
    m_profiles.push_back(makeHomesteadProfile());
    m_profiles.push_back(makeBerlinProfile());
    m_profiles.push_back(makeLondonProfile());
    m_profiles.push_back(makeParisProfile());
    m_profiles.push_back(makeShanghaiProfile());
    m_profiles.push_back(makeCancunProfile());
    m_profiles.push_back(makePragueProfile());
    m_profiles.push_back(makeOsakaProfile());
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

std::vector<std::string_view> ForkProfileRegistry::allProfileIds() const
{
    std::vector<std::string_view> ids;
    ids.reserve(m_profiles.size());
    for (auto const& profile : m_profiles)
    {
        ids.emplace_back(profile.profileId);
    }
    return ids;
}

std::optional<std::string_view> ForkProfileRegistry::profileIdForDirSegment(
    std::string_view segment) const
{
    auto const fork = upstreamForkFromDirSegment(segment);
    if (!fork.has_value())
    {
        return std::nullopt;
    }
    for (auto const& profile : m_profiles)
    {
        if (matchesForkName(profile, *fork))
        {
            return profile.profileId;
        }
    }
    return std::nullopt;
}

ForkProfile ForkProfileRegistry::resolveExecutionProfile(
    ForkProfile profile, std::optional<std::string> const& postFork) const
{
    if (!postFork.has_value())
    {
        return profile;
    }

    auto const postProfile = findByUpstreamFork(*postFork);
    if (!postProfile.has_value())
    {
        return profile;
    }

    auto const vmRevision = profile.revision.revision;
    profile.revision = postProfile->revision;
    profile.revision.revision = vmRevision;
    return profile;
}

}  // namespace bcos::evm::reference_tests
