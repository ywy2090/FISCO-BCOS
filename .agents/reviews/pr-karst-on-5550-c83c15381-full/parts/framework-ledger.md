# Module: framework-ledger (full @ c83c15381)

Karst timestamp-schedule persist + fail-closed hash. Live genesis write uses `buildOpForkScheduleMetadata` then `writeOpForkScheduleMetadata`. Boot resolve is fail-closed on stored hash/genesis binding. No new live-path correctness findings. One leftover convention miss on the new test file.

## Findings

### [FL1] New metadata test truncates Apache grant
**Severity:** LOW
**Origin:** INTRODUCED
**Scope:** tests
**Location:** bcos-ledger/test/unittests/ledger/test_OpForkScheduleMetadata.cpp:1
**Evidence:**
```
/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */
#include "L2GenesisTestStorage.h"
```
**Problem:** Sibling 2026 ledger tests (`test_GenesisEthHeader.cpp`, etc.) carry the full Apache grant + `@file`/`@brief`. This new file stops after SPDX. Also uses `kIsthmusJovianSchedule` (`c_` is the repo constant prefix). Prior license fix covered `OpForkId.h`/`OpTime.h` only — leftover half-fix.
**Why it matters:** License/Doxygen scanners that pass sibling tests fail this TU; `k_` vs `c_` drifts into copied production constants.
**Recommended fix:** Copy the full Apache block + `@file`/`@brief` from a sibling ledger test; rename to `c_isthmusJovianSchedule`.

## Checked, not a problem

1. **Karst without Jovian is rejected** (`OpForkScheduleCodec.h:158`): `if (hasKarst && baseline == "isthmus" && !hasJovian) throwInvalidOpForkSchedule("Jovian activation is required before Karst");` — genesis test pins `"0:karst"` and `"0:isthmus,1:karst"`.
2. **Stored hash is fail-closed** (`ChainMetadata.h:135`): `if (keccakOpForkScheduleHash(stored->schedule) != stored->scheduleHash) throwInvalidOpForkSchedule("op fork schedule hash mismatch");`
3. **Persist canonicalizes then hashes** (`ChainMetadata.h:88` / `Ledger.cpp:2623`): `auto normalized = canonicalOpForkSchedule(parseOpForkSchedule(canonical));` then `buildOpForkScheduleMetadata(*genesis.m_opstackForkSchedule, header->hash())`.
4. **Timestamp overflow is pre-multiply guarded** (`OpForkScheduleCodec.h:93`): `if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10)`.
5. **Stored resolve return is already canonical on the live path** (`ChainMetadata.h:88` then `:140`): `readOpForkScheduleMetadata` → `buildOpForkScheduleMetadata` writes `.schedule = std::move(normalized)` before `return stored->schedule`.

## Could not determine

- `crypto::HashType{hex}` length/`0x` behaviour (`parseOpForkScheduleHexHash`, `ChainMetadata.h:73`) — CommonType.h not in this module; carry-forward LOW “hash hex length” already covers it.
- Genesis header unix time vs accepted `"0:jovian,1:karst"` (`Ledger.cpp` header construction above the persist slice at 2621).

Reviewed with fisco-review v1.20.1
