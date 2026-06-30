/*
 *  ADR-033 compatibility shim — include DeductIntrinsicGas.h instead.
 */
#pragma once

#include "bcos-evm/eth/pipeline/DeductIntrinsicGas.h"

namespace bcos::evm
{
using IntrinsicGasDebitParams [[deprecated("use DeductIntrinsicGasParams (ADR-033)")]] =
    DeductIntrinsicGasParams;
using IntrinsicGasDebitOutcome [[deprecated("use DeductIntrinsicGasOutcome (ADR-033)")]] =
    DeductIntrinsicGasOutcome;
}  // namespace bcos::evm
