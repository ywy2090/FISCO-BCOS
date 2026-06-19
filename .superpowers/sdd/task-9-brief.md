### Task 9: Deposit execution path

**Files:**
- Modify: `bcos-evm/opstack/OpStackExecuteViaHost.cpp`
- Create: `bcos-evm/test/opstack/DepositMintTest.cpp`, `DepositNoFeeRoutingTest.cpp`

**Interfaces:**
- Implements §7.6: mint before checkpoint, checkpoint, executeMessage, failure revert+nonce++, success settlement without §7.4

- [ ] **Step 1: `DepositMintTest`** — mint credited before EVM

- [ ] **Step 2: Deposit branch in `opStackExecuteViaHost`** §9 step 4

- [ ] **Step 3: `DepositNoFeeRoutingTest`** — no credits to 0x19/0x1A/0x1B/coinbase

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(opstack): deposit execution path"
```

---

