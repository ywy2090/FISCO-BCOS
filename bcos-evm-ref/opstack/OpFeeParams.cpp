#include <bcos-evm-ref/opstack/OpFeeParams.h>

namespace bcos::evmref::opstack
{
namespace
{
// 读大端 word 的 [byteOff, byteOff+len) 为无符号整数（len ≤ 8）。
uint64_t readBE(const evmc::bytes32& w, size_t byteOff, size_t len) noexcept
{
    uint64_t v = 0;
    for (size_t i = 0; i < len; ++i)
    {
        v = (v << 8) | w.bytes[byteOff + i];
    }
    return v;
}
}  // namespace

OpFeeParams unpackOpFeeParams(const evmc::bytes32& slot1, const evmc::bytes32& slot3,
    const evmc::bytes32& slot7, const evmc::bytes32& slot8) noexcept
{
    return OpFeeParams{
        .l1_base_fee = intx::be::load<intx::uint256>(slot1),
        .base_fee_scalar = static_cast<uint32_t>(readBE(slot3, 16, 4)),
        .blob_base_fee_scalar = static_cast<uint32_t>(readBE(slot3, 20, 4)),
        .blob_base_fee = intx::be::load<intx::uint256>(slot7),
        .operator_fee_scalar = static_cast<uint32_t>(readBE(slot8, 20, 4)),
        .operator_fee_constant = readBE(slot8, 24, 8),
    };
}
}  // namespace bcos::evmref::opstack
