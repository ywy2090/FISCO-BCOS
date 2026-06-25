#pragma once

#include <bcos-utilities/Common.h>

namespace bcos::evm::reference_tests
{

/// Strict EIP-7702 (type 0x04) envelope validation for EEST transaction_tests.
/// Returns true only when RLP shape matches a well-formed typed transaction.
bool validateStrictEip7702TypedTx(bcos::bytesConstRef txbytes);

}  // namespace bcos::evm::reference_tests
