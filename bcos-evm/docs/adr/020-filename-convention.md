# ADR-020: bcos-evm Source Filename Convention

**Status:** Accepted  
**Date:** 2026-06-24  
**Related:** ADR-005, ADR-019, `docs/superpowers/specs/2026-06-24-bcos-evm-filename-convention-design.md`

---

## Context

~90% of `bcos-evm/` sources already use PascalCase filenames, but ADR-019 orchestration headers were introduced in camelCase (matching function names), and legacy `eth/state/` contains snake_case basenames alongside PascalCase `.hpp` files. This breaks the physical-module alignment of the three-library architecture (`eth/` kernel + `bcos/` / `opstack/` shells).

---

## Decision

### 1. Canonical rule

All new and migrated source files under `eth/`, `bcos/`, `opstack/`, `test/`, and `specs-tests/` **must** use:

- **PascalCase** basename (no snake_case, no camelCase filenames)
- **`.h`** for headers, **`.cpp`** for translation units (target state)
- Names reflect **modules/concepts**, not camelCase function names

Function identifiers (e.g. `debitIntrinsicGas()`) remain camelCase per C++ convention.

### 2. EIP and layer prefixes

| Category | Pattern | Example |
| --- | --- | --- |
| EIP-specific | `Eip<N>.h` | `Eip7702.h` |
| Chain shell | Layer prefix + domain | `FiscoPolicy.h`, `OpStackFee.h` |
| Orchestration step | PascalCase verb phrase | `IntrinsicGasDebit.h` |
| Domain model | No prefix | `RevisionConfig.h` |

### 3. Basename uniqueness

No two files in `bcos-evm/` may share the same basename. `opstack/OpStackBlobTxIntent.h` is renamed to `OpStackBlobTxIntent.h` to disambiguate from `eth/gas/Eip4844.h`.

### 4. `eth/state/` Legacy Enclave — dual-track

| Track | Rule | Phase |
| --- | --- | --- |
| **Basename** | PascalCase everywhere (incl. `BloomFilter.hpp`, `Transition.hpp`) | 1b |
| **Extension** | `.hpp` allowed only until Phase 3 migration | 3 |

Reviewers seeing `#include "…/State.hpp"` know: basename is compliant; extension migration is scheduled.

### 5. Phased migration

| Phase | Scope | Status |
| --- | --- | --- |
| 1 | Active layers: `eth/` (excl. state snake_case), `bcos/`, `opstack/` | Planned |
| 1b | `eth/state/` snake_case → PascalCase basename (6 files) | Planned (same PR as 1) |
| 2 | `test/` directory mirror | Planned |
| 3 | `eth/state/` `.hpp → .h` (~153 refs) | Deferred |
| 4 | `include/bcos-evm/*.hpp` | Deferred |

### 6. CI enforcement

`bcos-evm/tools/ci/check-filename-convention.sh` runs in `capability-gate.yml`:

- **Fail** on non-PascalCase basename in scoped dirs
- **Allow** `.hpp` extension only under `eth/state/` and `include/bcos-evm/`

---

## Consequences

- **Positive:** File names reinforce module boundaries; orchestration reads as one deep module; `ExecuteMessage.cpp` includes are visually consistent (PascalCase basenames).
- **Negative:** One-time rename churn (15 files, ~55 includes) in Phase 1+1b.
- **Neutral:** Function and type identifiers unchanged; only paths and `#include` lines move.
