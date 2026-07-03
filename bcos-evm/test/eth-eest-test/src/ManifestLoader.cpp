#include "bcos-evm/eth-eest-test/ManifestLoader.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <fstream>
#include <stdexcept>
#include <string>

namespace bcos::evm::reference_tests
{
namespace
{
namespace pt = boost::property_tree;

[[noreturn]] void throwManifestError(std::string const& message)
{
    throw std::runtime_error(message);
}

EvidenceKind parseEvidenceKind(std::string const& value)
{
    if (value == "ReferenceParity")
    {
        return EvidenceKind::ReferenceParity;
    }
    if (value == "BaselineReachability")
    {
        return EvidenceKind::BaselineReachability;
    }
    if (value == "DeviationAssertion")
    {
        return EvidenceKind::DeviationAssertion;
    }
    if (value == "UpstreamLiteralParity")
    {
        return EvidenceKind::UpstreamLiteralParity;
    }
    throwManifestError("Unknown evidenceKind: " + value);
}

ExecutionPath parseExecutionPath(std::string const& value)
{
    if (value == "Reference")
    {
        return ExecutionPath::Reference;
    }
    if (value == "BcosBaseline")
    {
        return ExecutionPath::BcosBaseline;
    }
    if (value == "OpStackBaseline")
    {
        return ExecutionPath::OpStackBaseline;
    }
    throwManifestError("Unknown path: " + value);
}

std::vector<std::string> parseStringArray(pt::ptree const& node, char const* field)
{
    std::vector<std::string> values;
    for (auto const& [key, child] : node.get_child(field))
    {
        static_cast<void>(key);
        values.push_back(child.get_value<std::string>());
    }
    if (values.empty())
    {
        throwManifestError(std::string("Manifest field '") + field + "' must be a non-empty array");
    }
    return values;
}

std::string requireString(pt::ptree const& node, char const* field)
{
    auto const value = node.get_optional<std::string>(field);
    if (!value.has_value() || value->empty())
    {
        throwManifestError(std::string("Manifest entry missing required field '") + field + "'");
    }
    return *value;
}

ManifestEntry parseManifestEntry(pt::ptree const& node)
{
    ManifestEntry entry;
    entry.evidenceId = requireString(node, "evidenceId");
    entry.sourceSuite = requireString(node, "sourceSuite");
    entry.casePath = requireString(node, "casePath");
    if (auto const variantKey = node.get_optional<std::string>("variantKey"))
    {
        if (variantKey->empty())
        {
            throwManifestError("Manifest field 'variantKey' must not be empty when present");
        }
        entry.variantKey = *variantKey;
    }
    entry.forkProfileId = requireString(node, "forkProfileId");
    entry.path = parseExecutionPath(requireString(node, "path"));
    entry.evidenceKind = parseEvidenceKind(requireString(node, "evidenceKind"));
    entry.capabilityRowIds = parseStringArray(node, "capabilityRowIds");
    entry.assertLevels = parseStringArray(node, "assertLevels");
    if (auto const postFork = node.get_optional<std::string>("postFork"))
    {
        if (postFork->empty())
        {
            throwManifestError("Manifest field 'postFork' must not be empty when present");
        }
        entry.postFork = *postFork;
    }
    if (auto const excludeNode = node.get_child_optional("excludeCaseFiles"))
    {
        for (auto const& [key, child] : *excludeNode)
        {
            static_cast<void>(key);
            auto const name = child.get_value<std::string>();
            if (!name.empty())
            {
                entry.excludeCaseFiles.push_back(name);
            }
        }
    }
    return entry;
}

pt::ptree readJsonTree(std::filesystem::path const& jsonPath)
{
    std::ifstream input(jsonPath);
    if (!input.good())
    {
        throwManifestError("Failed to open manifest: " + jsonPath.string());
    }

    pt::ptree tree;
    pt::read_json(input, tree);
    return tree;
}

}  // namespace

std::vector<ManifestEntry> loadManifest(std::filesystem::path const& jsonPath)
{
    auto const tree = readJsonTree(jsonPath);
    std::vector<ManifestEntry> entries;

    if (auto const entriesNode = tree.get_child_optional("entries"))
    {
        for (auto const& [key, child] : *entriesNode)
        {
            static_cast<void>(key);
            entries.push_back(parseManifestEntry(child));
        }
    }
    else
    {
        for (auto const& [key, child] : tree)
        {
            static_cast<void>(key);
            entries.push_back(parseManifestEntry(child));
        }
    }

    if (entries.empty())
    {
        throwManifestError("Manifest contains no entries: " + jsonPath.string());
    }

    return entries;
}

}  // namespace bcos::evm::reference_tests
