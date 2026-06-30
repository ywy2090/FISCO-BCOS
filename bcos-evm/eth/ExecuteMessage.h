/*
 *  ADR-033 compatibility shim — include InnerExecute.h instead.
 */
#pragma once

#include "bcos-evm/eth/InnerExecute.h"

namespace bcos::evm
{
using ExecuteMessageInput [[deprecated("use InnerExecuteInput (ADR-033)")]] = InnerExecuteInput;
using ExecuteMessageOutput [[deprecated("use InnerExecuteOutput (ADR-033)")]] = InnerExecuteOutput;
}  // namespace bcos::evm
