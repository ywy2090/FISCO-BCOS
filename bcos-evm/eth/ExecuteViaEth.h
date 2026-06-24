#pragma once

#include "bcos-evm/eth/EthReferenceBridge.h"

namespace bcos::evm
{
using ExecuteViaEthInput [[deprecated("use EthReferenceRequest")]] = EthReferenceRequest;
using ExecuteViaEthOutput [[deprecated("use EthReferenceResult")]] = EthReferenceResult;

[[deprecated("use ethReferenceExecute")]] inline task::Task<EthReferenceResult> executeViaEth(
    EthReferenceRequest input)
{
    return ethReferenceExecute(std::move(input));
}
}  // namespace bcos::evm
