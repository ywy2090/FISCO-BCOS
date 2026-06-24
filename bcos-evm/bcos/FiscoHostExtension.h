#pragma once

#include "bcos-evm/bcos/FiscoVmHostPolicy.h"

namespace bcos::evm
{
using FiscoHostExtension [[deprecated("use FiscoVmHostPolicy")]] = FiscoVmHostPolicy;
using FiscoHostExtensionDeps [[deprecated("use FiscoVmHostPolicyDeps")]] = FiscoVmHostPolicyDeps;
}  // namespace bcos::evm
