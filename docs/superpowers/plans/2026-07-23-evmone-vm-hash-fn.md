# evmone VM-carried hash_fn Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the SM3 hash hook from the concretized `evmc_host_context` onto `evmone::VM`, restoring upstream-opaque evmc headers and shrinking `ports/evmone/fisco-sm3.patch` from ~500 to ~150 lines.

**Architecture:** hash_fn becomes a public member of `evmone::VM` (set by FISCO after `evmc_create_evmone()` via the already-established `static_cast<evmone::VM*>` pattern). `ExecutionState` captures `{vm.hash_fn, host_ctx}` at each execute; the KECCAK256 instruction routes through it. Both executors stop inheriting `evmc_host_context` (which returns to upstream opaque); bcos-evm never sets hash_fn and keeps upstream keccak semantics untouched.

**Tech Stack:** C++20, vcpkg overlay port (`ports/evmone`), evmone v0.21.0, Boost.Test suites.

**Spec:** `docs/superpowers/specs/2026-07-23-evmone-vm-hash-fn-design.md`

## Global Constraints

- Work on branch `refactor-evmone-vm-hash-fn` (based on `evmone-official-dep`). Never push to or amend `evmone-official-dep` (PR #5351 is live).
- Prefix every shell command with `rtk` per project CLAUDE.md (`rtk git …`, `rtk cargo …`); raw commands shown below get the prefix at execution time.
- The pre-commit hook runs `clang-format -i` on staged files. If commit fails with "Format check failed", `git add` the reformatted files and commit again (never bypass with `--no-verify`).
- ABI-ghost rule: after any evmone header/struct change, results only count after the port is rebuilt (port-version bump) **and** FISCO targets recompiled. Never judge a test run from a stale incremental build.
- Test-methodology rule: `TransactionExecutorImpl/proxyReceive` FAILS in a full-suite run (pre-existing in-process ordering dependency, identical on base). Judge it **isolated-vs-isolated only**.
- Known pre-existing ctest failures to ignore: `TestTiKVStorage/*` (needs live TiKV), `test-rpbft`, `WsToolsTest/test_WsToolsTest`.
- Behavior invariants (spec §3): SM chain KECCAK256→SM3 unchanged; bcos-evm stays keccak even on SM nodes; the `noexcept` removal and `-fno-exceptions` removal stay in the patch as **fork parity** (review-corrected: they do NOT enable exception propagation — intermediate frames are still noexcept; do not restore them here either, that is a separate behavioral experiment).
- Two-commit split (spec §10): commit 1 = evmone port side; commit 2 = FISCO side. Commit 1 alone does not build FISCO — accepted by spec.

---

### Task 1: Regenerate fisco-sm3.patch (evmone side)

**Files:**
- Modify: `ports/evmone/fisco-sm3.patch` (full regeneration via a/b tree diff)
- Modify: `ports/evmone/vcpkg.json` (port-version 5→6)
- Modify: `ports/evmone/portfile.cmake` (comment lines 1–6 only)

**Interfaces:**
- Consumes: pristine ipsilon/evmone v0.21.0 source archive (vcpkg downloads); current patch (applies cleanly to pristine).
- Produces: patched evmone exposing `evmone::VM::hash_fn` member with exact signature `evmc_bytes32 (*)(struct evmc_host_context*, const uint8_t*, size_t)` (default `nullptr`), and `ExecutionState::hash_fn` / `ExecutionState::host_ctx` public members. `evmc_host_context` back to upstream opaque `struct evmc_host_context;`. Tasks 3–4 rely on these exact names.

- [ ] **Step 1: Locate the pristine v0.21.0 source archive**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS
ls vcpkg/downloads/ | grep -i evmone
```

Expected: a `vcpkg_from_github`-style archive (name contains `ipsilon` and/or `v0.21.0`, e.g. `ipsilon-evmone-v0.21.0.tar.gz`). The three `evmone-<sha>.tar.gz` files are old git-era fetches — do NOT use them.

Fallback if absent:

```bash
curl -L -o /private/tmp/evmone-v0.21.0.tar.gz https://github.com/ipsilon/evmone/archive/refs/tags/v0.21.0.tar.gz
shasum -a 512 /private/tmp/evmone-v0.21.0.tar.gz
```

Expected: SHA512 equals the value in `ports/evmone/portfile.cmake` line 15 (`bc2928d…f1d0f2`). If it does not match, STOP — wrong source.

- [ ] **Step 2: Extract two trees `a` (pristine) and `b` (work)**

```bash
W=/private/tmp/evmone-regen && rm -rf $W && mkdir -p $W && cd $W
tar xzf <ARCHIVE_PATH>
mv evmone-* a          # archive root dir starts with evmone- or ipsilon-evmone-
cp -R a b
grep -n "^struct evmc_host_context;" a/evmc/include/evmc/evmc.h
```

Expected: the grep prints one line (opaque declaration) — confirms pristine.

- [ ] **Step 3: Apply the current patch to `b`, then revert all `evmc/` changes**

```bash
cd $W/b
patch -p1 < /Users/octopus/octo/code/FISCO-BCOS/ports/evmone/fisco-sm3.patch
rm -rf evmc && cp -R ../a/evmc .
```

Expected: `patch` reports all hunks applied, no rejects. After the copy, `b/evmc` is upstream-identical (drops the evmc.h concretization, all evmc.hpp changes incl. WrappedHostContext, and mocked_host.hpp).

- [ ] **Step 4: Edit `b/lib/evmone/vm.hpp` — add the hash_fn member**

Find:

```cpp
    bool cgoto = EVMONE_CGOTO_SUPPORTED;
```

Replace with:

```cpp
    bool cgoto = EVMONE_CGOTO_SUPPORTED;

    /// FISCO-BCOS: optional host-provided hash for the KECCAK256 opcode (SM3 support).
    /// Set by the embedder after evmc_create_evmone(); nullptr keeps upstream keccak256.
    evmc_bytes32 (*hash_fn)(struct evmc_host_context* context, const uint8_t* data, size_t size) =
        nullptr;
```

- [ ] **Step 5: Edit `b/lib/evmone/execution_state.hpp` — restore upstream ctor/reset, add members**

The current patch left `set_hash_fn(...)` calls that reference now-removed `evmc::internal::` helpers. Replace the ctor body:

```cpp
      : msg{&message}, host{host_interface, host_ctx}, rev{revision}, original_code{_code}
    {
        host.set_hash_fn(
            evmc::internal::get_hash_fn(host_ctx), evmc::internal::get_host_hash_context(host_ctx));
    }
```

with the upstream body:

```cpp
      : msg{&message}, host{host_interface, host_ctx}, rev{revision}, original_code{_code}
    {}
```

In `reset(...)`, replace:

```cpp
        host = {host_interface, host_ctx};
        host.set_hash_fn(
            evmc::internal::get_hash_fn(host_ctx), evmc::internal::get_host_hash_context(host_ctx));
        rev = revision;
```

with:

```cpp
        host = {host_interface, host_ctx};
        hash_fn = nullptr;
        host_ctx_for_hash = nullptr;
        rev = revision;
```

Then, immediately BEFORE the line `ExecutionState() noexcept = default;`, insert the two members:

```cpp
    /// FISCO-BCOS: optional host-provided hash for KECCAK256 (SM3), carried by evmone::VM.
    /// Assigned by the execute() entry points right after reset()/construction.
    evmc_bytes32 (*hash_fn)(struct evmc_host_context* context, const uint8_t* data, size_t size) =
        nullptr;
    evmc_host_context* host_ctx_for_hash = nullptr;

```

- [ ] **Step 6: Edit `b/lib/evmone/baseline_execution.cpp` — thread vm.hash_fn into state**

In `execute(VM& vm, const evmc_host_interface& host, evmc_host_context* ctx, ...)`, find:

```cpp
    auto& state = vm.get_execution_state(static_cast<size_t>(msg.depth));
    state.reset(msg, rev, host, ctx, analysis.raw_code());
```

Replace with:

```cpp
    auto& state = vm.get_execution_state(static_cast<size_t>(msg.depth));
    state.reset(msg, rev, host, ctx, analysis.raw_code());
    state.hash_fn = vm.hash_fn;
    state.host_ctx_for_hash = ctx;
```

- [ ] **Step 7: Edit `b/lib/evmone/advanced_execution.cpp` — same threading (parity)**

Find:

```cpp
evmc_result execute(evmc_vm* /*unused*/, const evmc_host_interface* host, evmc_host_context* ctx,
    evmc_revision rev, const evmc_message* msg, const uint8_t* code, size_t code_size)
{
    const bytes_view container{code, code_size};
    const auto analysis = analyze(rev, container);
    auto state = std::make_unique<AdvancedExecutionState>(*msg, rev, *host, ctx, container);
    return execute(*state, analysis);
}
```

Replace with:

```cpp
evmc_result execute(evmc_vm* c_vm, const evmc_host_interface* host, evmc_host_context* ctx,
    evmc_revision rev, const evmc_message* msg, const uint8_t* code, size_t code_size)
{
    const bytes_view container{code, code_size};
    const auto analysis = analyze(rev, container);
    auto state = std::make_unique<AdvancedExecutionState>(*msg, rev, *host, ctx, container);
    state->hash_fn = static_cast<evmone::VM*>(c_vm)->hash_fn;
    state->host_ctx_for_hash = ctx;
    return execute(*state, analysis);
}
```

And add near the top of the file, after the existing includes:

```cpp
#include "vm.hpp"
```

(Note: the `noexcept`-removal on these signatures from the current patch is already present in `b` from Step 3 — leave it.)

- [ ] **Step 8: Edit `b/lib/evmone/instructions.hpp` — KECCAK256 routing via state**

The current patched block reads:

```cpp
    auto data = s != 0 ? &state.memory[i] : nullptr;
    if (const auto hash_fn = state.host.get_hash_fn(); hash_fn != nullptr)
    {
        size = intx::be::load<uint256>(state.host.hash(data, s));
    }
    else
    {
        size = intx::be::load<uint256>(ethash::keccak256(data, s));
    }
```

Replace with:

```cpp
    auto data = s != 0 ? &state.memory[i] : nullptr;
    if (state.hash_fn != nullptr)
    {
        const evmc_bytes32 h = state.hash_fn(state.host_ctx_for_hash, data, s);
        size = intx::be::load<uint256>(reinterpret_cast<const evmc::bytes32&>(h));
    }
    else
    {
        size = intx::be::load<uint256>(ethash::keccak256(data, s));
    }
```

(`evmc::bytes32 : evmc_bytes32` with no extra members — the reference cast is layout-safe; the current patch already relies on `intx::be::load<uint256>` over `evmc::bytes32`.)

- [ ] **Step 9: Edit `b/lib/evmone/CMakeLists.txt` — drop the dead cmake-config hunk (review Finding)**

The current patch appends a `write_basic_package_version_file` / `Config.cmake.in` / `configure_package_config_file` / `install(FILES ...)` block whose only output (`lib/cmake/evmone/*`) the portfile deletes unconditionally. Delete that entire appended block from `b/lib/evmone/CMakeLists.txt` — everything from:

```cmake
write_basic_package_version_file(
```

through the closing:

```cmake
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
)
```

Keep everything above it (including the `add_standalone_library(evmone)` line and the `-fno-exceptions` removal already applied by the current patch).

- [ ] **Step 10: Hygiene — clean the noexcept-removal residue in `b` (review Finding)**

```bash
cd /private/tmp/evmone-regen/b
sed -i '' -e 's/) $/)/' -e 's/) ;/);/' \
  lib/evmone/advanced_execution.cpp lib/evmone/advanced_execution.hpp \
  lib/evmone/baseline.hpp lib/evmone/baseline_execution.cpp
grep -rn ") $\|) ;" lib/evmone/ | head
```

Expected: final grep prints nothing (no trailing `) ` or `) ;` tokens remain).

- [ ] **Step 11: Regenerate the patch and install it**

```bash
cd $W
diff -ruN a b > /Users/octopus/octo/code/FISCO-BCOS/ports/evmone/fisco-sm3.patch
wc -l /Users/octopus/octo/code/FISCO-BCOS/ports/evmone/fisco-sm3.patch
grep -c "evmc/include" /Users/octopus/octo/code/FISCO-BCOS/ports/evmone/fisco-sm3.patch
```

Expected: line count in the 130–200 range; the second grep prints `0` (no evmc header hunks at all).

- [ ] **Step 12: Verify the new patch applies cleanly to pristine**

```bash
cd $W && rm -rf c && cp -R a c && cd c
patch -p1 --dry-run < /Users/octopus/octo/code/FISCO-BCOS/ports/evmone/fisco-sm3.patch
```

Expected: every hunk "succeeded", zero rejects.

- [ ] **Step 13: Bump port-version, clean the portfile (review Findings), refresh its comment**

In `ports/evmone/vcpkg.json`: `"port-version": 5,` → `"port-version": 6,`.

In `ports/evmone/portfile.cmake`:

1. DELETE step 1b (lines 20–25, the `vcpkg_replace_string` on `lib/evmone/CMakeLists.txt`) — its needle has an escaping bug and has NEVER matched (build log: "vcpkg_replace_string made no changes"); its target text no longer exists after patch Step 9 anyway.
2. DELETE step 2 (lines 27–32, the `EXPORT evmoneTargets` removal) — official v0.21.0 has no `install(EXPORT)`/`export()` anywhere, so the export set never exists and the edit is dead logic.
3. DELETE step 4b (the 6-header curated `file(INSTALL ...baseline.hpp vm.hpp execution_state.hpp tracing.hpp constants.hpp delegation.hpp...)` block) — fully redundant with the patch's `install(DIRECTORY .../lib/evmone/ ... PATTERN "*.hpp")`.
4. In the hand-written config, give `evmone::precompiles` a debug location (latent MSVC LNK2038 / silent release-mix on Unix). Change:

```cmake
    add_library(evmone::precompiles STATIC IMPORTED)
    set_target_properties(evmone::precompiles PROPERTIES
        IMPORTED_LOCATION
            "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_evmone_lib_prefix}evmone_precompiles${_evmone_lib_suffix}"
        INTERFACE_LINK_LIBRARIES "blst"
    )
```

to:

```cmake
    add_library(evmone::precompiles STATIC IMPORTED)
    set_target_properties(evmone::precompiles PROPERTIES
        IMPORTED_LOCATION_RELEASE
            "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_evmone_lib_prefix}evmone_precompiles${_evmone_lib_suffix}"
        IMPORTED_LOCATION_DEBUG
            "${CMAKE_CURRENT_LIST_DIR}/../../debug/lib/${_evmone_lib_prefix}evmone_precompiles${_evmone_lib_suffix}"
        IMPORTED_LOCATION
            "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_evmone_lib_prefix}evmone_precompiles${_evmone_lib_suffix}"
        INTERFACE_LINK_LIBRARIES "blst"
    )
```

5. Replace comment lines 1–6 with:

```cmake
# Official evmone (ipsilon) v0.21.0. FISCO-BCOS's SM3 (national crypto) support is
# applied as a transparent patch instead of consuming a private fork: evmone::VM
# carries an optional host-provided hash_fn used by the KECCAK256 opcode, so SM
# chains hash with SM3. The evmc/ headers are NOT modified (evmc_host_context stays
# opaque upstream). The patch also carries the macOS static-lib combine and the
# fork-parity exception-enabled build (noexcept stripped from the execute entry
# points; NOT an exception-propagation guarantee). See fisco-sm3.patch.
```

---

### Task 2: Rebuild the port, verify installed headers, commit evmone side

**Files:**
- No source edits; consumes Task 1 outputs.

**Interfaces:**
- Produces: installed `build/vcpkg_installed/arm64-osx/include/evmc/evmc.h` with opaque `struct evmc_host_context;`, and `include/evmone/vm.hpp` / `include/evmone/execution_state.hpp` exposing `hash_fn` / `host_ctx_for_hash`. Tasks 3–4 compile against these.

- [ ] **Step 1: Reconfigure (vcpkg rebuilds evmone at port-version 6)**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS
cmake -S . -B build 2>&1 | tail -5
```

Expected: `Configuring done` / `Generating done`, exit 0. This step takes several minutes (evmone recompiles).

- [ ] **Step 2: Verify the installed headers**

```bash
grep -n "^struct evmc_host_context;" build/vcpkg_installed/arm64-osx/include/evmc/evmc.h
grep -n "hash_fn" build/vcpkg_installed/arm64-osx/include/evmone/vm.hpp
grep -n "host_ctx_for_hash" build/vcpkg_installed/arm64-osx/include/evmone/execution_state.hpp
git grep -n "set_hash_fn\|get_hash_fn\|WrappedHostContext" -- '*.h' '*.cpp' ':!ports/*' ':!*/.claude/*' ':!*/.worktrees/*'
```

Expected: first three greps each print ≥1 line; the last prints **nothing** (no FISCO code referenced the removed evmc.hpp extensions).

- [ ] **Step 3: Commit 1 (evmone side)**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS
git add ports/evmone/fisco-sm3.patch ports/evmone/vcpkg.json ports/evmone/portfile.cmake
git commit -m "build(evmone): carry the SM3 hash hook on evmone::VM, restore upstream evmc headers

fisco-sm3.patch no longer concretizes evmc_host_context or touches any
evmc/ header: hash_fn moves to an evmone::VM member, threaded into
ExecutionState at each execute and consumed by the KECCAK256 opcode.
Drops WrappedHostContext, the magic-tag dispatch and the internal::
callback rewrites. Both evm_hash_fn implementations ignore their context
argument and dispatch via the process-global g_hashImpl, so a VM-carried
pointer is sufficient; per-VM carry (not a process global) keeps bcos-evm
on upstream keccak even on SM nodes.

NOTE: FISCO-BCOS does not compile against this commit alone; the executor
adaptation lands in the next commit (accepted two-commit split)."
```

Expected: commit created on `refactor-evmone-vm-hash-fn`.

---

### Task 3: Legacy executor (bcos-executor) de-inherit + VM hash_fn set points

**Files:**
- Modify: `bcos-executor/src/vm/HostContext.h:41` (class head; add `interface` member; declare `evm_hash_fn`)
- Modify: `bcos-executor/src/vm/HostContext.cpp:72-74` (drop `hash_fn = evm_hash_fn;`)
- Modify: `bcos-executor/src/vm/EVMHostInterface.cpp` (16 casts)
- Modify: `bcos-executor/src/vm/VMInstance.cpp:44-81` (set `vm->hash_fn`; cast ctx at 2 call sites)

**Interfaces:**
- Consumes: `evmone::VM::hash_fn` member (Task 1); opaque `evmc_host_context` (Task 2).
- Produces: `bcos::executor::evm_hash_fn` declared in `HostContext.h` with signature `evmc_bytes32 evm_hash_fn(evmc_host_context* context, const uint8_t* data, size_t size);` — Task 4 uses the same pattern for executor_v1. `HostContext` gains a public `const evmc_host_interface* hostInterface` member (renamed from the old inherited `interface` — that spelling collides with the Windows SDK `#define interface struct` macro; review finding).

- [ ] **Step 1: HostContext.h — drop the base class, add the interface member and evm_hash_fn declaration**

At line 41, change:

```cpp
class HostContext : public evmc_host_context
{
public:
    using UniquePtr = std::unique_ptr<HostContext>;
```

to:

```cpp
class HostContext
{
public:
    /// EVMC host interface table (formerly the inherited `interface` field of the
    /// concretized evmc_host_context; the evmc type is upstream-opaque again).
    /// Renamed: `interface` is a Windows SDK macro (#define interface struct).
    const evmc_host_interface* hostInterface = nullptr;

    using UniquePtr = std::unique_ptr<HostContext>;
```

Then add, in namespace scope near the class (next to the other free-function declarations in the header):

```cpp
/// Host-provided hash for the KECCAK256 opcode; dispatches via GlobalHashImpl::g_hashImpl
/// (SM3 on SM chains). Installed on evmone::VM by VMInstance.
evmc_bytes32 evm_hash_fn(evmc_host_context* context, const uint8_t* data, size_t size);
```

- [ ] **Step 2: HostContext.cpp — remove the ctx-field hash assignment, rename the interface write**

At lines 72–74, change:

```cpp
    interface = getHostInterface();

    hash_fn = evm_hash_fn;
```

to:

```cpp
    hostInterface = getHostInterface();
```

(Keep the `evm_hash_fn` definition at line ~59 exactly as is — it is now installed on the VM by VMInstance instead.)

- [ ] **Step 3: EVMHostInterface.cpp — sweep the 16 casts**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS
sed -i '' \
  -e 's/static_cast<HostContext&>(\*_context)/*reinterpret_cast<HostContext*>(_context)/g' \
  -e 's/static_cast<HostContext&>(\*context)/*reinterpret_cast<HostContext*>(context)/g' \
  bcos-executor/src/vm/EVMHostInterface.cpp
grep -c "static_cast<HostContext&>" bcos-executor/src/vm/EVMHostInterface.cpp
grep -c "reinterpret_cast<HostContext\*>" bcos-executor/src/vm/EVMHostInterface.cpp
```

Expected: `0` then `16`. Also add this comment at the top of the file, under the existing header comment:

```cpp
// NOTE: evmc_host_context is upstream-opaque; every context pointer entering these
// shims is the executor's own HostContext (see VMInstance), so the reinterpret_casts
// below are the inverse of the cast made at the execute() call sites.
```

- [ ] **Step 4: VMInstance.cpp — install hash_fn on the VM; cast ctx at call sites**

Change the C-ABI ctor (line ~44):

```cpp
VMInstance::VMInstance(evmc_vm* instance, evmc_revision revision, bytes_view code) noexcept
  : m_evmcVm(evmc::VM{instance}), m_revision(revision), m_code(code)
{
    assert(m_evmcVm->is_abi_compatible());
```

to:

```cpp
VMInstance::VMInstance(evmc_vm* instance, evmc_revision revision, bytes_view code) noexcept
  : m_evmcVm(evmc::VM{instance}), m_revision(revision), m_code(code)
{
    assert(m_evmcVm->is_abi_compatible());
    static_cast<evmone::VM*>(m_evmcVm->get_raw_pointer())->hash_fn = evm_hash_fn;
```

Change `execute()` (lines ~64–81):

```cpp
Result VMInstance::execute(HostContext& _hostContext, evmc_message* _msg)
{
    if (m_evmcVm)
    {
        return Result(m_evmcVm
                          ->execute(*_hostContext.interface, &_hostContext, m_revision, *_msg,
                              m_code.data(), m_code.size())
                          .release_raw());
    }
```

to:

```cpp
Result VMInstance::execute(HostContext& _hostContext, evmc_message* _msg)
{
    auto* hostCtx = reinterpret_cast<evmc_host_context*>(&_hostContext);
    if (m_evmcVm)
    {
        return Result(m_evmcVm
                          ->execute(*_hostContext.hostInterface, hostCtx, m_revision, *_msg,
                              m_code.data(), m_code.size())
                          .release_raw());
    }
```

and the fresh-VM path:

```cpp
    evmc::VM evm{evmc_create_evmone()};
    return Result(evmone::baseline::execute(*static_cast<evmone::VM*>(evm.get_raw_pointer()),
        *_hostContext.interface, &_hostContext, m_revision, *_msg, *m_analysis));
```

to:

```cpp
    evmc::VM evm{evmc_create_evmone()};
    auto* evmoneVm = static_cast<evmone::VM*>(evm.get_raw_pointer());
    evmoneVm->hash_fn = evm_hash_fn;
    return Result(evmone::baseline::execute(
        *evmoneVm, *_hostContext.hostInterface, hostCtx, m_revision, *_msg, *m_analysis));
```

(`VMInstance.cpp` already includes `HostContext.h` and `<evmone/vm.hpp>` — no include changes.)

- [ ] **Step 5: Build and run the bcos-executor suite**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS
cmake --build build --target test-bcos-executor 2>&1 | grep -iE "error|warning: unused" | head -20
build/bcos-executor/test/unittest/test-bcos-executor 2>&1 | tail -3
```

Expected: no compile errors; suite ends with `*** No errors detected` (351 cases — covers SM3 hashing, EVM keccak/sha3 via host hash, and the V3.0/V3.1 precompiled-gas boundary).

Do NOT commit yet (commit 2 covers both executors, end of Task 4).

---

### Task 4: executor_v1 (transaction-executor) de-inherit + VM hash_fn set point

**Files:**
- Modify: `transaction-executor/bcos-transaction-executor/vm/HostContext.h:128` (class head), `:207` (ctor init), `:885` (this→ctx cast)
- Modify: `transaction-executor/bcos-transaction-executor/vm/EVMHostInterface.h` (16 casts)
- Modify: `transaction-executor/bcos-transaction-executor/vm/VMInstance.cpp` (set `vm->hash_fn`)

**Interfaces:**
- Consumes: `evmone::VM::hash_fn` (Task 1); `bcos::executor_v1::hostcontext::evm_hash_fn` (existing, `HostContext.cpp:7`, signature `evmc_bytes32 evm_hash_fn(evmc_host_context*, const uint8_t*, size_t)`).
- Produces: nothing consumed by later tasks (Task 5 is verification only).

- [ ] **Step 1: HostContext.h — drop the base, add the interface member first in the class**

At line 128, change:

```cpp
class HostContext : public evmc_host_context
{
private:
    std::reference_wrapper<Storage> m_rollbackableStorage;
```

to:

```cpp
class HostContext
{
private:
    /// EVMC host interface table (formerly the inherited `interface` field of the
    /// concretized evmc_host_context; the evmc type is upstream-opaque again).
    /// Renamed: `interface` is a Windows SDK macro. Declared first so the
    /// mem-init list order below matches declaration order.
    const evmc_host_interface* m_hostInterface = nullptr;

    std::reference_wrapper<Storage> m_rollbackableStorage;
```

- [ ] **Step 2: HostContext.h:207 — replace the base aggregate-init with a member init**

Change:

```cpp
      : evmc_host_context{.interface = hostInterface, .hash_fn = evm_hash_fn},
        m_rollbackableStorage(storage),
```

to:

```cpp
      : m_hostInterface(hostInterface),
        m_rollbackableStorage(storage),
```

- [ ] **Step 3: HostContext.h:885 — explicit ctx cast for `this`**

Change:

```cpp
        co_return m_executable->m_vmInstance.execute(interface, this, m_revision,
```

to:

```cpp
        co_return m_executable->m_vmInstance.execute(m_hostInterface,
            reinterpret_cast<evmc_host_context*>(this), m_revision,
```

- [ ] **Step 4: EVMHostInterface.h — sweep the 16 casts**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS
sed -i '' \
  -e 's/static_cast<HostContextType&>(\*context)/*reinterpret_cast<HostContextType*>(context)/g' \
  transaction-executor/bcos-transaction-executor/vm/EVMHostInterface.h
grep -c "static_cast<HostContextType&>" transaction-executor/bcos-transaction-executor/vm/EVMHostInterface.h
grep -c "reinterpret_cast<HostContextType\*>" transaction-executor/bcos-transaction-executor/vm/EVMHostInterface.h
```

Expected: `0` then `16`. Add the same one-paragraph NOTE comment as Task 3 Step 3 at the top of the file (adjusting `HostContext` → `HostContextType`).

- [ ] **Step 5: VMInstance.cpp — install hash_fn on the fresh VM**

Change:

```cpp
    evmc::VM evm{evmc_create_evmone()};
    return EVMCResult(evmone::baseline::execute(
        *static_cast<evmone::VM*>(evm.get_raw_pointer()), *host, context, rev, *msg, *m_analysis));
```

to:

```cpp
    evmc::VM evm{evmc_create_evmone()};
    auto* evmoneVm = static_cast<evmone::VM*>(evm.get_raw_pointer());
    evmoneVm->hash_fn = hostcontext::evm_hash_fn;
    return EVMCResult(
        evmone::baseline::execute(*evmoneVm, *host, context, rev, *msg, *m_analysis));
```

And add near the top of the file (after the existing includes):

```cpp
namespace bcos::executor_v1::hostcontext
{
evmc_bytes32 evm_hash_fn(struct evmc_host_context* context, const uint8_t* data, size_t size);
}  // namespace bcos::executor_v1::hostcontext
```

(Forward declaration avoids including the heavy `HostContext.h` from this small TU.)

- [ ] **Step 6: Build and run the transaction-executor suite**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS
cmake --build build --target test-transaction-executor 2>&1 | grep -iE "error" | head -20
build/transaction-executor/tests/test-transaction-executor --run_test=TransactionExecutorImpl/proxyReceive 2>&1 | tail -2
build/transaction-executor/tests/test-transaction-executor 2>&1 | tail -6
```

Expected: no compile errors; **isolated** proxyReceive prints `*** No errors detected`; the full-suite run shows ONLY the known pre-existing proxyReceive ordering failures (`:776/:779/:802/:805`) and nothing else new.

- [ ] **Step 7: Commit 2 (FISCO side)**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS
git add bcos-executor/src/vm/HostContext.h bcos-executor/src/vm/HostContext.cpp \
  bcos-executor/src/vm/EVMHostInterface.cpp bcos-executor/src/vm/VMInstance.cpp \
  transaction-executor/bcos-transaction-executor/vm/HostContext.h \
  transaction-executor/bcos-transaction-executor/vm/EVMHostInterface.h \
  transaction-executor/bcos-transaction-executor/vm/VMInstance.cpp
git commit -m "refactor(executor): stop inheriting evmc_host_context; install hash_fn on evmone::VM

evmc_host_context is upstream-opaque again, so both executors' HostContext
classes hold the evmc_host_interface pointer as a plain member and pass
themselves as an opaque context (reinterpret_cast at the execute call sites
and in the host-interface shims, which are each other's inverse). The SM3
hash hook is installed on the evmone::VM object at every VM creation site
(legacy C-ABI ctor, legacy fresh-VM path, executor_v1 fresh-VM path);
bcos-evm never installs it and keeps upstream keccak256."
```

If the pre-commit hook reformats files, re-`git add` them and commit again.

---

### Task 5: HashFnRoutingTest — seam-level assertion of the KECCAK256 routing

**Files:**
- Create: `bcos-executor/test/unittest/evmone/HashFnRoutingTest.cpp`

**Interfaces:**
- Consumes: `evmone::VM::hash_fn` (Task 1), upstream `evmc::VM::execute(const evmc_host_interface&, evmc_host_context*, ...)`.
- Produces: nothing (leaf test). Collected automatically — `bcos-executor/test/unittest/CMakeLists.txt` uses `file(GLOB_RECURSE SOURCES "*.cpp" ...)`, no CMake edit needed.

Closes the review-identified gap: the fork's evmone-level hook test was dropped and no FISCO test asserts the KECCAK256 *opcode* routing. A sentinel hash fn proves routing directly (independent of SM3 availability); a second VM without hash_fn proves the upstream-keccak fallback.

- [ ] **Step 1: Write the test**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Seam test: KECCAK256 opcode routes through evmone::VM::hash_fn when set,
 *         falls back to upstream ethash::keccak256 when not.
 *  @file HashFnRoutingTest.cpp
 */
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>
#include <evmone/evmone.h>
#include <evmone/vm.hpp>
#include <cstring>

namespace bcos::test
{

namespace
{
// PUSH1 0, PUSH1 0, KECCAK256, PUSH1 0, MSTORE, PUSH1 32, PUSH1 0, RETURN
// => returns the 32-byte hash of the empty input.
constexpr uint8_t kKeccakEmptyBytecode[] = {
    0x60, 0x00, 0x60, 0x00, 0x20, 0x60, 0x00, 0x52, 0x60, 0x20, 0x60, 0x00, 0xf3};

// keccak256("") — well-known constant.
constexpr uint8_t kKeccakEmptyDigest[32] = {0xc5, 0xd2, 0x46, 0x01, 0x86, 0xf7, 0x23, 0x3c, 0x92,
    0x7e, 0x7d, 0xb2, 0xdc, 0xc7, 0x03, 0xc0, 0xe5, 0x00, 0xb6, 0x53, 0xca, 0x82, 0x27, 0x3b, 0x7b,
    0xfa, 0xd8, 0x04, 0x5d, 0x85, 0xa4, 0x70};

evmc_bytes32 sentinelHash(evmc_host_context* /*context*/, const uint8_t* /*data*/, size_t /*size*/)
{
    evmc_bytes32 h{};
    for (size_t i = 0; i < sizeof(h.bytes); ++i)
    {
        h.bytes[i] = static_cast<uint8_t>(i + 1);
    }
    return h;
}

// No host callback fires for this bytecode; a zeroed interface is sufficient.
const evmc_host_interface kEmptyHostInterface{};

evmc::Result runKeccakEmpty(bool withSentinel)
{
    evmc::VM vm{evmc_create_evmone()};
    if (withSentinel)
    {
        static_cast<evmone::VM*>(vm.get_raw_pointer())->hash_fn = sentinelHash;
    }
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = 100000;
    return vm.execute(kEmptyHostInterface, nullptr, EVMC_SHANGHAI, msg, kKeccakEmptyBytecode,
        sizeof(kKeccakEmptyBytecode));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(HashFnRoutingTest)

BOOST_AUTO_TEST_CASE(keccak256OpcodeUsesVmHashFnWhenSet)
{
    auto result = runKeccakEmpty(true);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(result.output_size, 32U);
    for (size_t i = 0; i < 32; ++i)
    {
        BOOST_CHECK_EQUAL(result.output_data[i], static_cast<uint8_t>(i + 1));
    }
}

BOOST_AUTO_TEST_CASE(keccak256OpcodeFallsBackToKeccakWhenUnset)
{
    auto result = runKeccakEmpty(false);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(result.output_size, 32U);
    BOOST_CHECK_EQUAL(std::memcmp(result.output_data, kKeccakEmptyDigest, 32), 0);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
```

- [ ] **Step 2: Build and run the two cases**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS
cmake --build build --target test-bcos-executor 2>&1 | grep -iE "error" | head
build/bcos-executor/test/unittest/test-bcos-executor --run_test=HashFnRoutingTest 2>&1 | tail -3
```

Expected: no compile errors; `*** No errors detected`. The sentinel case fails if routing is broken; the fallback case fails if hash_fn leaks between VMs.

- [ ] **Step 3: Fold into commit 2 (branch is unpushed — amend is safe)**

```bash
git add bcos-executor/test/unittest/evmone/HashFnRoutingTest.cpp
git commit --amend --no-edit
```

Expected: commit 2 now contains the 7 refactor files + the new test.

---

### Task 6: Full verification

**Files:** none (verification only; fixes → amend commit 2).

**Interfaces:** consumes everything above; produces the green state required by spec §9.

- [ ] **Step 1: Full build, all targets**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS
cmake --build build 2>&1 | grep -iE "error:|FAILED:" | grep -v "ld: warning" | head -20
```

Expected: empty output (exit 0). This recompiles every dependent of the changed evmone headers — required before judging any test (ABI-ghost rule).

- [ ] **Step 2: bcos-evm invariant (§3.3)**

```bash
find build/bcos-evm -type f -perm +111 -name "*est*" | head -3
# run the EthTransition test binary found above, e.g.:
build/bcos-evm/test/eth/<binary> 2>&1 | tail -3
```

Expected: `*** No errors detected` — bcos-evm computes upstream keccak with no hash_fn installed.

- [ ] **Step 3: Full ctest**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/build
ctest --output-on-failure -j4 2>&1 | tail -25
```

Expected: same failure set as the pre-change baseline and nothing more: `TestTiKVStorage/*` (12, needs live server), `test-rpbft`, `WsToolsTest/test_WsToolsTest`. `proxyReceive` PASSES under ctest (per-process execution). Any NEW failure = regression: diagnose, fix, amend commit 2, re-run this task.

- [ ] **Step 4: Final state check**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS
git log --oneline evmone-official-dep..HEAD
git status --short | grep -v "^??"
```

Expected: the docs commits (spec/plan and their review revision) followed by exactly two code commits — evmone side (Task 2) and FISCO side (Task 4, amended by Task 5); clean tree.
