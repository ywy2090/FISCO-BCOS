# ADR-017: FISCO Precompile Port

**Status:** Accepted  
**Date:** 2026-06-23  
**Related:** ADR-005, spec 2026-06-23-precompile-port-design v1.2

## Decision

FISCO chain precompile 执行与 auth 经 `ChainExtendedPrecompileDispatch` / `AuthPort` 注入（ADR-024 扩展 ADR-017，取代 `ChainPrecompilePort` + `tryChainPrecompile`）；
implementation 驻 `transaction-executor/adapters/` + `bcos-executor` precompiled TU；
`bcos-evm` 源码零 `bcos-executor` include。

## Consequences

- compile-boundary grep 可固化（A-2）
- bcos-evm 单测可 mock Port
- PrecompileRouter（kernel）与 Port（chain）正交
