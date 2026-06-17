#pragma once
#include <evmc/evmc.h>
#include <cstddef>
#include <cstdint>

namespace bcos::evm::precompiles
{

// ═══════════════════════════════════════════════════════════════════════
// O(1) compile-time Ethereum precompile registry
// Modeled after evmone test/state/precompiles.cpp traits[] design.
// Single source of truth — no macros, no singletons, no heap.
// ═══════════════════════════════════════════════════════════════════════

struct PrecompileTraits
{
    uint16_t address_suffix;      // 地址的低 16 位 (0x0001=ecRecover, 0x0100=p256verify)
    evmc_revision since;          // 从哪个 EVM fork 开始激活
    evmc_revision deprecated_in;  // EVMC_MAX_REVISION = 从未废弃
    int64_t gas_base;             // 基础 gas（-1 = 使用 revision-aware pricer）
    int64_t gas_per_word;         // 每字 gas（-1 = 使用 revision-aware pricer）
};

// ──────────────────────────────────────────────────────────────
// 完整注册表：Ethereum 全部 18 个预编译
// ──────────────────────────────────────────────────────────────
inline constexpr PrecompileTraits ALL_ETHEREUM_PRECOMPILES[] = {
    // Frontier
    {0x0001, EVMC_FRONTIER,  EVMC_MAX_REVISION, 3000,  0},    // ecrecover
    {0x0002, EVMC_FRONTIER,  EVMC_MAX_REVISION, 60,   12},    // sha256
    {0x0003, EVMC_FRONTIER,  EVMC_MAX_REVISION, 600,  120},   // ripemd160
    {0x0004, EVMC_FRONTIER,  EVMC_MAX_REVISION, 15,   3},     // identity

    // Byzantium
    {0x0005, EVMC_BYZANTIUM, EVMC_MAX_REVISION, -1,   -1},    // modexp (revision-aware)
    {0x0006, EVMC_BYZANTIUM, EVMC_MAX_REVISION, 150,  0},     // alt_bn128_G1_add
    {0x0007, EVMC_BYZANTIUM, EVMC_MAX_REVISION, 6000, 0},     // alt_bn128_G1_mul
    {0x0008, EVMC_BYZANTIUM, EVMC_MAX_REVISION, -1,   -1},    // alt_bn128_pairing (EIP-1108)

    // Istanbul
    {0x0009, EVMC_ISTANBUL,  EVMC_MAX_REVISION, -1,   -1},    // blake2f (EIP-152)

    // Cancun
    {0x000a, EVMC_CANCUN,    EVMC_MAX_REVISION, 50000, 0},    // point_evaluation (EIP-4844)

    // Prague — BLS12-381 (EIP-2537)
    {0x000b, EVMC_PRAGUE,    EVMC_MAX_REVISION, 500,   0},    // bls12_g1add
    {0x000c, EVMC_PRAGUE,    EVMC_MAX_REVISION, -1,   -1},    // bls12_g1msm (discount table)
    {0x000d, EVMC_PRAGUE,    EVMC_MAX_REVISION, 800,   0},    // bls12_g2add
    {0x000e, EVMC_PRAGUE,    EVMC_MAX_REVISION, -1,   -1},    // bls12_g2msm (discount table)
    {0x000f, EVMC_PRAGUE,    EVMC_MAX_REVISION, -1,   -1},    // bls12_pairing_check
    {0x0010, EVMC_PRAGUE,    EVMC_MAX_REVISION, 5500,  0},    // bls12_map_fp_to_g1
    {0x0011, EVMC_PRAGUE,    EVMC_MAX_REVISION, 75000, 0},    // bls12_map_fp2_to_g2

    // Osaka
    {0x0100, EVMC_OSAKA,     EVMC_MAX_REVISION, 6900,  0},    // p256verify (RIP-7212)
};

inline constexpr size_t PRECOMPILE_COUNT =
    sizeof(ALL_ETHEREUM_PRECOMPILES) / sizeof(ALL_ETHEREUM_PRECOMPILES[0]);

// ──────────────────────────────────────────────────────────────
// O(1) 地址 → 索引：取地址最后 2 字节
// ──────────────────────────────────────────────────────────────
constexpr uint16_t toLookupIndex(const evmc_address& addr) noexcept
{
    return static_cast<uint16_t>(
        (addr.bytes[18] << 8) | addr.bytes[19]);
}

// 编译期查找表大小
constexpr size_t LOOKUP_SIZE = 0x0101;  // 覆盖 0x0001..0x0100

// ──────────────────────────────────────────────────────────────
// O(1) 查找：地址是否是一个已激活的以太坊预编译
// ──────────────────────────────────────────────────────────────
constexpr const PrecompileTraits* findPrecompile(
    evmc_revision rev, const evmc_address& addr) noexcept
{
    const auto idx = toLookupIndex(addr);
    for (const auto& t : ALL_ETHEREUM_PRECOMPILES)
    {
        if (t.address_suffix == idx)
            return (t.since <= rev && rev <= t.deprecated_in) ? &t : nullptr;
    }
    return nullptr;
}

inline bool isEthereumPrecompile(evmc_revision rev, const evmc_address& addr) noexcept
{
    return findPrecompile(rev, addr) != nullptr;
}

// ──────────────────────────────────────────────────────────────
// Gas 计算：返回 -1 的字段表示需要 revision-aware pricer
// ──────────────────────────────────────────────────────────────
inline bool hasRevisionAwarePricer(const PrecompileTraits* t) noexcept
{
    return t && (t->gas_base == -1 || t->gas_per_word == -1);
}

}  // namespace bcos::evm::precompiles
