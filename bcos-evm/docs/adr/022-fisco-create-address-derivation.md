# ADR-022: FISCO CREATE Address Derivation (Single Module)

**Status:** Proposed  
**Date:** 2026-06-25  
**Related:** ADR-005, ADR-017, ADR-019, `eth/execution/CreateContract.h`, `architecture-review-post-orchestration-2026-06-23.md` (候选 6)

---

## Context

FISCO BCOS uses **two CREATE address schemes**:

| Scheme | When | Formula |
| --- | --- | --- |
| **Legacy Web3** | Ethereum-compatible addressing | `newLegacyEVMAddress(deployer, nonce)` |
| **FISCO native** | Pre-Web3 / internal txs | `hashImpl("{blockNumber}_{contextID}_{seq}")` → 20-byte address |

CREATE2 follows the standard `0xff ‖ deployer ‖ salt ‖ hash(initcode)` envelope (with FISCO `hashImpl` for initcode digest), but **deployer selection** differs between top-level and nested frames.

Today the logic is **copy-pasted across four seams**:

| Seam | Location | Role |
| --- | --- | --- |
| Orchestration (top-level) | `bcos/ApplyFiscoMessage.cpp::deriveMessage()` | `FiscoStateTransitionBindings::txSetupMessage` |
| Policy duplicate | `bcos/FiscoPolicy.h::deriveMessageImpl()` | Compat tests only; byte-identical to above |
| VmHostPolicy (nested) | `bcos/FiscoVmHostPolicy.cpp::deriveNestedCreateAddress()` | `prepareMessage` inside kernel call tree |
| Post-execute patch | `bcos/FiscoStateTransitionErrorPolicy::onFinalizeGasUsed()` | Fill empty `create_address` from `recipient` (not re-derivation) |

ETH reference path already converged on `eth/execution/CreateContract.h` (`predictCreateAddress`, `bindCreateMessageForInit`). FISCO has no equivalent.

### Observed drift (2026-06-25 audit)

| # | Decision point | Top-level `deriveMessage()` | Nested `deriveNestedCreateAddress()` | Legacy `TransactionExecutive` (bcos-executor) |
| --- | --- | --- | --- | --- |
| D1 | Legacy mode gate | `web3Tx` only | `web3Tx \|\| feature_evm_address` | `feature_evm_address` (top + nested) |
| D2 | Legacy nonce source | `input.nonce` (tx param) | `state.get_nonce(deployer)` | `input.nonce` |
| D3 | Legacy / CREATE2 deployer | `message.sender` | `callerAddress` if non-zero, else `sender` | `senderAddress` string from input |
| D4 | FISCO hash `seq` | `input.seq` (no increment) | `++*nestedSeq` | `newSeq` passed in |
| D5 | CREATE empty guard | Only write when `code_address == EMPTY` | Always overwrite | Only when `receiveAddress` empty |
| D6 | `hashImpl == nullptr` | Early return | Early return | N/A |

**D1 + D2** are the highest-risk gaps: `feature_evm_address` ON with `web3Tx == false` yields FISCO-hash at top-level but legacy at nested; nested legacy reads live state nonce while top-level uses the tx nonce parameter.

Compat tests (`CompatExecuteViaHostTest`, `CompatHostContextTest`) predict nested addresses via `FiscoPolicy::deriveMessage(..., childSeq = seq + 1)` — equivalent to seam ②, **not** seam ③. They align for FISCO-hash + non-Web3 when `childSeq` matches post-increment `nestedSeq`, but not for D1/D2/D3 combinations.

---

## Decision

### 1. Introduce `bcos/FiscoAddressDerivation.h` (header-only or `.cpp` TU in `bcos-evm-bcos`)

Single module owns **all address computation**. No CREATE address math in `FiscoPolicy`, `FiscoExecute`, or `FiscoVmHostPolicy` bodies after migration.

Two public entry points:

```cpp
struct FiscoTopLevelCreateParams {
    bool web3Tx;
    bool featureEvmAddress;          // ledger::Features::Flag::feature_evm_address
    evmc_message const& message;
    protocol::BlockNumber blockNumber;
    int64_t contextID;
    int64_t seq;                     // caller-owned; not incremented here
    u256 txNonce;                    // transaction nonce for legacy top-level
    crypto::Hash const& hashImpl;
};

struct FiscoNestedCreateParams {
    bool web3Tx;
    bool featureEvmAddress;
    evmc_message const& message;
    evmc_address const& origin;
    evmc_address const& callerAddress;  // from VmHostPolicy::setCallerAddress
    protocol::BlockNumber blockNumber;
    int64_t contextID;
    int64_t* nestedSeq;                 // incremented in-place on FISCO-hash path
    state::State const& state;
    crypto::Hash const& hashImpl;
};

// Returns computed address; does not mutate message unless bind helper used.
evmc_address predictFiscoTopLevelCreateAddress(FiscoTopLevelCreateParams const&);
evmc_address predictFiscoNestedCreateAddress(FiscoNestedCreateParams const&);

// Optional: bind recipient + code_address on message (mirrors CreateContract.h pattern).
void bindFiscoCreateMessage(evmc_message& message, evmc_address const& addr);
```

**CREATE2** helpers share one internal `predictFiscoCreate2Address(deployer, salt, initcode, hashImpl)`.

### 2. Normative routing table (post-consolidation)

#### 2.1 When each entry point runs

| Frame | Condition | Entry point | Caller |
| --- | --- | --- | --- |
| Top-level | `depth == 0` && EOA sender (`sender == origin`) | `predictFiscoTopLevelCreateAddress` | `FiscoStateTransitionBindings::txSetupMessage` via `deriveMessage()` wrapper |
| Nested | `depth > 0` OR `sender != origin` | `predictFiscoNestedCreateAddress` | `FiscoVmHostPolicy::prepareMessage` |
| Post-execute | CREATE success && empty `create_address` | **No derivation** — copy `message.recipient` | `FiscoStateTransitionErrorPolicy::onFinalizeGasUsed` (unchanged) |

#### 2.2 Legacy vs FISCO-hash gate (**resolves D1**)

**Decision:** `useLegacyAddress = web3Tx || featureEvmAddress` at **both** top-level and nested.

Rationale: matches legacy `TransactionExecutive` and removes top-level-only FISCO-hash when `feature_evm_address` is ON.

#### 2.3 Nonce source for legacy CREATE (**resolves D2**)

**Decision:** Parameterized by frame:

| Frame | Legacy nonce input | Rationale |
| --- | --- | --- |
| Top-level | `txNonce` from orchestration input (current `input.nonce`) | Tx not yet executed; matches TE Prepare path |
| Nested | `state.get_nonce(deployer)` at `prepareMessage` time | Contract-initiated CREATE must observe deployer nonce after prior state changes in same tx |

Document explicitly in module header: nested legacy **intentionally** differs from top-level nonce source (same as current VmHostPolicy, unlike stale `deriveMessage` copy used in compat tests).

**Follow-up (optional):** if product requires strict `TransactionExecutive` parity for nested legacy, switch nested to caller-supplied nonce — requires TE to pass nested nonce through Host; defer unless regression found.

#### 2.4 Deployer for CREATE / CREATE2 (**resolves D3**)

| Frame | Deployer |
| --- | --- |
| Top-level | `message.sender` |
| Nested | `callerAddress` if non-zero, else `message.sender` |

Nested deployer rule stays; top-level unchanged. Align CREATE2 top-level with nested deployer rule **only if** a regression test proves TE passes non-zero caller at depth 0 (currently out of scope).

#### 2.5 FISCO-hash `seq` (**resolves D4**)

| Frame | `seq` behavior |
| --- | --- |
| Top-level | Use `params.seq` as-is; increment is **caller's** responsibility before invoke |
| Nested | `++*nestedSeq` inside `predictFiscoNestedCreateAddress` |

Document contract: `FiscoExecutionRequest.seq` is the outer seq at tx entry; `nestedSeq` pointer is shared with VmHostPolicy for inner CREATE accounting.

#### 2.6 EMPTY `code_address` guard (**resolves D5**)

| Kind | Top-level | Nested |
| --- | --- | --- |
| CREATE | Derive only if `code_address == EMPTY_EVM_ADDRESS` | Always derive (overwrite) |
| CREATE2 | Always derive | Always derive |

Nested CREATE always overwrites preserves current VmHostPolicy behavior for inner frames where evmone may leave stale fields.

### 3. Seam ownership after migration

| Seam | After ADR-022 |
| --- | --- |
| `deriveMessage()` | Thin wrapper: fill `FiscoTopLevelCreateParams` → `predictFiscoTopLevelCreateAddress` → `bindFiscoCreateMessage` |
| `FiscoPolicy::deriveMessageImpl` | **Removed**; compat tests call `deriveMessage()` or `predictFiscoTopLevelCreateAddress` directly |
| `deriveNestedCreateAddress()` | Thin wrapper around `predictFiscoNestedCreateAddress` + depth/origin early exit |
| `onFinalizeGasUsed` CREATE patch | Unchanged (not derivation) |

`FiscoPolicy` remains **RevisionConfig + auth + timestamp** only; no address math (extends ADR-005 / ADR-017 port direction).

### 4. Testing contract

Add `bcos-evm/test/bcos/FiscoAddressDerivationTest.cpp` (skeleton landed 2026-06-25):

| Case | Assert |
| --- | --- |
| Top-level FISCO hash | `hash("{block}_{ctx}_{seq}")` |
| Top-level legacy Web3 | `newLegacyEVMAddress(sender, txNonce)` |
| Top-level `feature_evm_address` without Web3 | legacy (post D1 fix) |
| Nested FISCO hash | `++nestedSeq` then hash |
| Nested legacy | `state.get_nonce(deployer)` |
| Nested CREATE2 deployer | caller vs sender |
| Top vs nested D1/D2 matrix | No accidental divergence |
| `deriveMessage()` ≡ top-level helper | Regression guard |

Migrate compat tests to import `FiscoAddressDerivation.h` for **nested** predictions (replace `FiscoPolicy::deriveMessage` for nested frames).

### 5. Capability matrix

Add row:

| Capability | Layer | ETH | BCOS | OP | Tests |
| --- | --- | --- | --- | --- | --- |
| FISCO CREATE address derivation | orchestration + VmHostPolicy | unsupported | explicit (`FiscoAddressDerivation`) | unsupported | `FiscoAddressDerivationTest`, Fisco execution smoke |

---

## Consequences

- **Behavior change (intentional):** top-level CREATE with `feature_evm_address == true` && `web3Tx == false` switches from FISCO-hash to legacy — aligns with `TransactionExecutive` and nested path. Requires matrix note + snapshot test.
- **No change** to post-execute `create_address` patch or CREATE nonce init (`applyCreateNonceSemantics`, ADR-005).
- **No change** to ETH `CreateContract.h` path.
- Reduces candidate 6 «four modules» to one module + two thin delegates + one post-execute patch.
- Enables deletion of ~120 lines of duplicate code in `FiscoPolicy.h`.

---

## Alternatives considered

| Alternative | Rejected because |
| --- | --- |
| Keep duplication; document only | D1/D2 drift remains; compat tests lie about nested behavior |
| Single entry point with `isNested` flag | Hides legitimately different nonce/seq/deployer rules; harder to test |
| Move all derivation into VmHostPolicy | Top-level CREATE runs before Host exists; violates ADR-005 orchestrator-before-kernel |
| Delete FISCO-hash scheme | Breaking change for non-Web3 deployments |

---

## Implementation plan (non-normative)

1. Add `FiscoAddressDerivation.h` + characterization tests (no call-site moves). **Done (PR-A).**
2. Rewire `deriveMessage()` and `deriveNestedCreateAddress()` to delegate. **Done (PR-A).**
3. Fix D1 at top-level (`featureEvmAddress` in `FiscoStateTransitionBindings` session / bridge input).
4. Remove `FiscoPolicy::deriveMessageImpl`; update compat tests.
5. Update `capability-matrix.md`, `architecture-known-gaps.md` (new Gap or close candidate 6 footnote).

---

## Compliance checklist

- [ ] All CREATE/CREATE2 address math lives in `FiscoAddressDerivation.*`.
- [ ] No new address derivation in `FiscoPolicy.h`.
- [ ] Top-level and nested follow §2 normative table.
- [ ] `FiscoAddressDerivationTest` covers D1–D5 matrix.
- [ ] Compat nested tests use nested helper, not top-level copy.
- [ ] `capability-matrix.md` row added.
- [ ] ADR-005 §3 VmHostPolicy table footnotes ADR-022 for CREATE address.

---

## Open questions (require product sign-off before Accepted)

1. **Nested legacy nonce:** keep `state.get_nonce(deployer)` (proposed) vs TE `input.nonce` parity?
2. **Top-level CREATE2 deployer:** always `sender`, or adopt callerAddress when set at depth 0?
3. **`hashImpl` algorithm:** FISCO-hash and CREATE2 initcode digest use `crypto::Hash` (SM3/Keccak per chain config) — document in matrix as `deviation` from Ethereum Keccak?
