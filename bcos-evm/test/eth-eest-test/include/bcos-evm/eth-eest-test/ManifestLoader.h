#pragma once

#include "bcos-evm/eth-eest-test/StateTestTypes.h"
#include <filesystem>
#include <vector>

namespace bcos::evm::reference_tests
{

std::vector<ManifestEntry> loadManifest(std::filesystem::path const& jsonPath);

}  // namespace bcos::evm::reference_tests
