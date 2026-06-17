#pragma once
#include <evmc/evmc.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace bcos::evm::precompiles
{

// ═══════════════════════════════════════════════════════════════════════
// O(1) compile-time Ethereum precompile registry
// Modeled after evmone test/state/precompiles.cpp traits[] design.
// Single source of truth — no macros, no singletons, no heap.
//
// Each trait holds:
//   - Fork activation window  (since .. deprecated_in)
//   - Gas constants           (base + per_word, -1 = revision-aware)
//   - Execution backend       (analyze + execute function pointers,
//                              wired once at startup by PrecompiledManager)
// ═══════════════════════════════════════════════════════════════════════

// ── 后端函数签名 ──────────────────────────────────────────────
// analyze: 验证输入，返回 gas 成本（-1 = 使用 revision-aware pricer）
// execute: 执行预编译，返回 (success, output_bytes)
using AnalyzeFn  = int64_t (*)(bytesConstRef input, evmc_revision rev);
using ExecuteFn  = std::pair<bool, bytes> (*)(bytesConstRef input);

struct PrecompileTraits
{
    uint16_t address_suffix;
    evmc_revision since;
    evmc_revision deprecated_in;
    int64_t gas_base;
    int64_t gas_per_word;
    AnalyzeFn analyze;    // ← 新增：输入端验证 + gas 计算
    ExecuteFn execute;    // ← 新增：实际执行
};

// ═══════════════════════════════════════════════════════════════════════
// 完整注册表：Ethereum 全部 18 个预编译
//
// analyze/execute 初始为 nullptr — 由 PrecompiledManager::initPrecompileTraits()
// 在启动时通过 PrecompiledRegistrar 按名称查找后端函数并填入。
// ═══════════════════════════════════════════════════════════════════════
inline PrecompileTraits ALL_ETHEREUM_PRECOMPILES[] = {
    // Frontier
    {0x0001, EVMC_FRONTIER,  EVMC_MAX_REVISION, 3000,  0,  nullptr, nullptr}, // ecrecover
    {0x0002, EVMC_FRONTIER,  EVMC_MAX_REVISION, 60,   12,  nullptr, nullptr}, // sha256
    {0x0003, EVMC_FRONTIER,  EVMC_MAX_REVISION, 600,  120, nullptr, nullptr}, // ripemd160
    {0x0004, EVMC_FRONTIER,  EVMC_MAX_REVISION, 15,   3,   nullptr, nullptr}, // identity

    // Byzantium
    {0x0005, EVMC_BYZANTIUM, EVMC_MAX_REVISION, -1,   -1,  nullptr, nullptr}, // modexp
    {0x0006, EVMC_BYZANTIUM, EVMC_MAX_REVISION, 150,  0,   nullptr, nullptr}, // alt_bn128_G1_add
    {0x0007, EVMC_BYZANTIUM, EVMC_MAX_REVISION, 6000, 0,   nullptr, nullptr}, // alt_bn128_G1_mul
    {0x0008, EVMC_BYZANTIUM, EVMC_MAX_REVISION, -1,   -1,  nullptr, nullptr}, // alt_bn128_pairing

    // Istanbul
    {0x0009, EVMC_ISTANBUL,  EVMC_MAX_REVISION, -1,   -1,  nullptr, nullptr}, // blake2f

    // Cancun
    {0x000a, EVMC_CANCUN,    EVMC_MAX_REVISION, 50000, 0,  nullptr, nullptr}, // point_evaluation

    // Prague — BLS12-381 (EIP-2537)
    {0x000b, EVMC_PRAGUE,    EVMC_MAX_REVISION, 500,   0,  nullptr, nullptr}, // bls12_g1add
    {0x000c, EVMC_PRAGUE,    EVMC_MAX_REVISION, -1,   -1,  nullptr, nullptr}, // bls12_g1msm
    {0x000d, EVMC_PRAGUE,    EVMC_MAX_REVISION, 800,   0,  nullptr, nullptr}, // bls12_g2add
    {0x000e, EVMC_PRAGUE,    EVMC_MAX_REVISION, -1,   -1,  nullptr, nullptr}, // bls12_g2msm
    {0x000f, EVMC_PRAGUE,    EVMC_MAX_REVISION, -1,   -1,  nullptr, nullptr}, // bls12_pairing_check
    {0x0010, EVMC_PRAGUE,    EVMC_MAX_REVISION, 5500,  0,  nullptr, nullptr}, // bls12_map_fp_to_g1
    {0x0011, EVMC_PRAGUE,    EVMC_MAX_REVISION, 75000, 0,  nullptr, nullptr}, // bls12_map_fp2_to_g2

    // Osaka
    {0x0100, EVMC_OSAKA,     EVMC_MAX_REVISION, 6900,  0,  nullptr, nullptr}, // p256verify
};

inline constexpr size_t PRECOMPILE_COUNT =
    sizeof(ALL_ETHEREUM_PRECOMPILES) / sizeof(ALL_ETHEREUM_PRECOMPILES[0]);

// ═══════════════════════════════════════════════════════════════════════
// 启动时初始化：从 PrecompiledRegistrar 按名称查找后端函数并填入 traits
// ═══════════════════════════════════════════════════════════════════════
inline const char* precompileName(uint16_t suffix) noexcept
{
    switch (suffix)
    {
    case 0x0001: return "ecrecover";
    case 0x0002: return "sha256";
    case 0x0003: return "ripemd160";
    case 0x0004: return "identity";
    case 0x0005: return "modexp";
    case 0x0006: return "alt_bn128_G1_add";
    case 0x0007: return "alt_bn128_G1_mul";
    case 0x0008: return "alt_bn128_pairing_product";
    case 0x0009: return "blake2_compression";
    case 0x000a: return "point_evaluation";
    case 0x000b: return "bls12_g1add";
    case 0x000c: return "bls12_g1msm";
    case 0x000d: return "bls12_g2add";
    case 0x000e: return "bls12_g2msm";
    case 0x000f: return "bls12_pairing_check";
    case 0x0010: return "bls12_map_fp_to_g1";
    case 0x0011: return "bls12_map_fp2_to_g2";
    case 0x0100: return "p256verify";
    default: return nullptr;
    }
}

// 初始化所有以太坊预编译的 execute 函数指针
// 调用时机：PrecompiledManager 构造函数中，在 registerAllBuiltins() 之后
inline void initPrecompileTraits()
{
    for (auto& t : ALL_ETHEREUM_PRECOMPILES)
    {
        const auto* name = precompileName(t.address_suffix);
        if (!name) continue;

        // execute 后端：从 PrecompiledRegistrar 按名称查找
        try { t.execute = PrecompiledRegistrar::executor(name).target<ExecuteFn>(); }
        catch (...) { t.execute = nullptr; }
    }
}

// ═══════════════════════════════════════════════════════════════════════
// O(1) 查找函数
// ═══════════════════════════════════════════════════════════════════════

constexpr uint16_t toLookupIndex(const evmc_address& addr) noexcept
{
    return static_cast<uint16_t>(
        (addr.bytes[18] << 8) | addr.bytes[19]);
}

constexpr size_t LOOKUP_SIZE = 0x0101;

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

inline bool hasRevisionAwarePricer(const PrecompileTraits* t) noexcept
{
    return t && (t->gas_base == -1 || t->gas_per_word == -1);
}

}  // namespace bcos::evm::precompiles
