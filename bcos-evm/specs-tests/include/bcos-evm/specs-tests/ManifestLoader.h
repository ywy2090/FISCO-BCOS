#pragma once

#include "bcos-evm/specs-tests/StateTestTypes.h"
#include <filesystem>
#include <vector>

namespace bcos::evm::reference_tests
{

std::vector<ManifestEntry> loadManifest(std::filesystem::path const& jsonPath);

}  // namespace bcos::evm::reference_tests
