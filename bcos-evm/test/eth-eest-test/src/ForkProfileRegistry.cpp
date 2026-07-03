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
    return eips;
}

PathProfile referenceParityProfile()
{
    PathProfile profile;
    profile.path = ExecutionPath::Reference;
    profile.evidenceKind = EvidenceKind::ReferenceParity;
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

}  // namespace

ForkProfileRegistry::ForkProfileRegistry()
{
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

}  // namespace bcos::evm::reference_tests
