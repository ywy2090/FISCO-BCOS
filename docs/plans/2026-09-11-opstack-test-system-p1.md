# OP-Stack 测试体系 P1 实施计划（oracle 工件化 + M1 矩阵 + M4 序列矩阵 + 变异骨架）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把「oracle 以生成工件形态存在」落成可执行流水线：两个 Go dumper（caps + op-node 版本窗）产出 `matrix/` 工件，M1 矩阵测试按工件断言每个 fork 的方法窗，M4 的 14 条枚举序列在不变量组 I1–I6 下逐步验证，并建成变异 harness（先跑通 NEW-3/N2/N1 三个变体）。

**Architecture:** 工件生成属于**外部语料库仓** `FISCO-BCOS/op-stack-e2e-tests`（本地 checkout `~/.cache/fisco-t8n-corpus`，经 symlink `opstack-executor/tests/t8n` 暴露给 FISCO 仓）；dumper 沿用既有 `regen.sh` 机制——把 Go 源拷进对应 pin 树、`go build`、写出工件。矩阵测试在 FISCO 仓的 `test-bcos-engine` 里只读工件，缺工件时 skip。变异 harness 在 FISCO 仓 `tools/mutation/`。

**Tech Stack:** Go 1.24（dumper，op-geth/op-node 各自 module 内 build；**两棵 pin 树的 go.mod 都写 `go 1.24`**，op-geth 还带 `tool` 块，`GOTOOLCHAIN=local` 会直接解析失败）、C++20 + Boost.Test（矩阵测试，unity 构建）、Bash（regen/mutation 脚本）、GitHub Actions（CI 接线）。

**Spec:** `docs/2026-09-11-opstack-test-system-design.md`（P1 对应其 §9「P1 验收判据」；格子语义见 `docs/2026-09-11-opstack-test-matrix-design.md`）。

**环境前置（已实测，勿再假设）**
- `go version` = go1.23.4 darwin/arm64（`/usr/local/go/bin/go`）——**但实际构建走 `GOTOOLCHAIN=auto` 自动下载 go1.24.0**（需网络；CI action 本身用 `go-version: '1.24.x'`）。离线或 `GOTOOLCHAIN=local` 时两棵 pin 树的 `go.mod` 会解析失败。
- 语料 op-geth pin 树：`/Users/octopus/octo/code/blockchain-impl/op-geth`，HEAD `e8800cffe53d459cde8a07c8e8f1de9d86e79e07`，**工作树干净**；module `github.com/ethereum/go-ethereum`。
- optimism pin 树：`/Users/octopus/octo/code/optimism` @ `76e4fad54244ec6bd07dad07e42c82a16ab5113a`，module `github.com/ethereum-optimism/optimism`；`go list ./op-node/rollup` 成功。
- 语料库仓本地 checkout：`~/.cache/fisco-t8n-corpus`（branch `main`，remote `https://github.com/FISCO-BCOS/op-stack-e2e-tests`），其 `opstack-executor/tests/t8n/` 经 symlink 进入 FISCO 工作树。
- **引用行号规则**：op-geth 一律 `git show <pin>:<path>`（本机 `code/op-geth` 工作树脏，纯注释改动，行号错位约 200 行）。
- 陷阱：`engine/test/CMakeLists.txt` 用 configure 期 `file(GLOB_RECURSE ...)` → **新增测试文件后必须重跑 cmake**；Boost 过滤用通配/单值，**逗号分隔不匹配**（会 `Test setup error: no test cases matching filter`）；套件名错会 0 例假绿，一律报用例数。

**工作目录约定**：标 `[corpus]` 的任务在 `~/.cache/fisco-t8n-corpus` 内执行并提交到该仓的一个新分支；标 `[fisco]` 的任务在 `/Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550` 内执行。

---

## 文件结构（先锁定分解）

**语料库仓 `[corpus]`**
- `opstack-executor/tests/t8n/generator/cmd/caps/main.go` —— caps dumper（类型反射，产 `matrix/caps.json`）。新建。
- `opstack-executor/tests/t8n/generator/cmd/opnodewin/main.go` —— op-node 版本窗 dumper（产 `matrix/engine_api_windows.json`）。新建。
- `opstack-executor/tests/t8n/generator/matrix/input_schedule.json` —— 9 fork 的时间轴输入（人工维护、入库）。新建。
- `opstack-executor/tests/t8n/generator/regen.sh` —— 增加两个 build+run 步骤与 `matrix/` 契约文件判据。修改。
- `.gitignore` —— **只**追加两个生成物的忽略规则：`opstack-executor/tests/t8n/matrix/caps.json` 与 `.../engine_api_windows.json`（不得用通配——`known_deviations.json` 必须入库）。修改。
- `opstack-executor/tests/t8n/matrix/known_deviations.json` —— 已记录的窗口偏离（人工维护、入库）。新建。

**FISCO 仓 `[fisco]`**
- `engine/test/unittests/engine/OpEngineApiMatrixTest.cpp` —— M1 矩阵测试（读工件）。新建。
- `engine/test/unittests/engine/support/SequenceInvariants.h` —— I1–I6 断言库。新建。
- `engine/test/unittests/engine/OpEngineSequenceMatrixTest.cpp` —— M4 的 14 条序列 + 随机序列（env 门控）。新建。
- `engine/test/unittests/engine/support/OpEngineKarstTestHarness.h` —— 承接从测试文件抽出的导入夹具（Task 7a）。修改。
- `engine/test/unittests/engine/OpEngineImportFcuTest.cpp` —— 删去被抽出的夹具段、改为 include（Task 7a）。修改。
- `engine/test/CMakeLists.txt` —— 加 `OP_MATRIX_DIR` 编译定义。修改。
- `tools/mutation/{run.sh,make-variant.sh,variants/mapping.json,variants/*.patch}` —— 变异 harness。新建。

---

## Task 0: 前置校验与语料库分支

**Files:** 无（只读校验 + 建分支）

- [ ] **Step 1: 校验工具链与 pin**

```bash
go version                                   # 本机 1.23.4 即可
# 但 dumper 构建需要 1.24：先确认 GOTOOLCHAIN 允许自动下载，或预置 go1.24
grep -m1 '^go ' /Users/octopus/octo/code/blockchain-impl/op-geth/go.mod   # 期望 go 1.24.0
grep -m1 '^go ' /Users/octopus/octo/code/optimism/go.mod                # 期望 go 1.24.0
git -C /Users/octopus/octo/code/blockchain-impl/op-geth rev-parse HEAD
# 期望 e8800cffe53d459cde8a07c8e8f1de9d86e79e07
git -C /Users/octopus/octo/code/blockchain-impl/op-geth status --porcelain | wc -l   # 期望 0
git -C /Users/octopus/octo/code/optimism rev-parse HEAD
# 期望 76e4fad54244ec6bd07dad07e42c82a16ab5113a
```
任一项不符则停止并在计划外先对齐 pin。

- [ ] **Step 2: 语料库仓建分支并确认 regen 基线可跑**

```bash
cd ~/.cache/fisco-t8n-corpus
git checkout -b feat/test-matrix-oracles
OPGETH=/Users/octopus/octo/code/blockchain-impl/op-geth \
  bash opstack-executor/tests/t8n/generator/regen.sh; echo "regen rc=$?"
```
期望 `rc=0`（这条既验证机制，也确认当前语料与 pin 一致）。

- [ ] **Step 3: 记录校验结果（无提交）**

把 Step 1 的两个 pin 值与 Step 2 的 rc 写进后续 Task 的 commit message 或 PR 描述。**不产生提交**。

---

## Task 1: `dump-geth-caps`（`[corpus]`）

**Files:**
- Create: `opstack-executor/tests/t8n/generator/cmd/caps/main.go`
- Create（生成物）: `opstack-executor/tests/t8n/matrix/caps.json`
- Modify: `.gitignore`

- [ ] **Step 1: 写 dumper（完整代码）**

```go
// Command caps emits the Engine API capability list that op-geth advertises by
// reflection, so the matrix can assert FISCO's own advertisement derives from the
// same naming rule. Reflection on the type is used because constructing
// ConsensusAPI requires a full eth backend.
package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"unicode"

	"github.com/ethereum/go-ethereum/eth/catalyst"
)

type artifact struct {
	Pin         string   `json:"pin"`
	GeneratedBy string   `json:"generated_by"`
	Caps        []string `json:"caps"`
}

func main() {
	if len(os.Args) != 3 {
		fmt.Fprintln(os.Stderr, "usage: caps <op-geth-pin> <out-dir>")
		os.Exit(2)
	}
	pin, outDir := os.Args[1], os.Args[2]
	// Same rule as op-geth's ConsensusAPI.ExchangeCapabilities
	// (eth/catalyst/api.go: pin d0734fd5 -> :1122-1133; the corpus pin e8800cffe -> :1123-1134;
	// the two bodies are byte-identical): "engine_" + lower-camel of every exported method
	// except the RPC entry point itself.
	t := reflect.TypeOf((*catalyst.ConsensusAPI)(nil))
	caps := make([]string, 0, t.NumMethod())
	for i := 0; i < t.NumMethod(); i++ {
		name := []rune(t.Method(i).Name)
		if string(name) == "ExchangeCapabilities" {
			continue
		}
		caps = append(caps, "engine_"+string(unicode.ToLower(name[0]))+string(name[1:]))
	}
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	f, err := os.Create(filepath.Join(outDir, "caps.json"))
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	defer f.Close()
	enc := json.NewEncoder(f)
	enc.SetIndent("", "  ")
	if err := enc.Encode(artifact{Pin: pin, GeneratedBy: "cmd/caps", Caps: caps}); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
```

- [ ] **Step 2: 构建并运行（复用 regen 的机制）**

```bash
cd ~/.cache/fisco-t8n-corpus
G=/Users/octopus/octo/code/blockchain-impl/op-geth
T8N=opstack-executor/tests/t8n
rm -rf "$G/cmd/opt8n-caps" && mkdir -p "$G/cmd/opt8n-caps"
cp "$T8N/generator/cmd/caps/main.go" "$G/cmd/opt8n-caps/main.go"
( cd "$G" && go build -o /tmp/opt8n-caps ./cmd/opt8n-caps )   # 期望无输出
/tmp/opt8n-caps e8800cffe53d459cde8a07c8e8f1de9d86e79e07 "$T8N/matrix"
python3 -c "import json;d=json.load(open('$T8N/matrix/caps.json'));print(d['pin'], len(d['caps']));print([c for c in d['caps'] if 'Payload' in c or 'Forkchoice' in c])"
rm -rf "$G/cmd/opt8n-caps"     # 立即清理，保持 op-geth 工作树干净
```
期望（**已实测**，本机跑过）：`caps.json` 共 **36** 条；含 `engine_newPayloadV1..V5`、`engine_forkchoiceUpdatedV1..V4`、`engine_getPayloadV1..V6`、`engine_getClientVersionV1`、`engine_getBlobsV1..V3`、`engine_executeStatelessPayloadV1..V4`、`engine_newPayloadWithWitnessV1..V4`、`engine_forkchoiceUpdatedWithWitnessV1..V3`、`engine_getPayloadBodiesBy*`；**不含** `engine_exchangeCapabilities`（op-geth 反射时跳过自身）。`NewPayloadV5`/`ForkchoiceUpdatedV4` 的存在即 §6 M1 对 S3-F9 的据。

- [ ] **Step 3: 写 `.gitignore` 规则**

```bash
cd ~/.cache/fisco-t8n-corpus
printf '%s\n' 'opstack-executor/tests/t8n/matrix/caps.json' \
              'opstack-executor/tests/t8n/matrix/engine_api_windows.json' >> .gitignore
git check-ignore -v opstack-executor/tests/t8n/matrix/caps.json   # 期望命中新规则
```

- [ ] **Step 4: 提交**

```bash
cd ~/.cache/fisco-t8n-corpus
git add opstack-executor/tests/t8n/generator/cmd/caps/main.go .gitignore
git commit -m "feat(generator): dump op-geth engine capability list for the matrix"
git status --porcelain   # 期望只余被忽略的生成物，无 tracked 改动
```

---

## Task 2: `dump-opnode` 与 `engine_api_windows.json`（`[corpus]`）

**Files:**
- Create: `opstack-executor/tests/t8n/generator/matrix/input_schedule.json`
- Create: `opstack-executor/tests/t8n/generator/cmd/opnodewin/main.go`
- Create（生成物）: `opstack-executor/tests/t8n/matrix/engine_api_windows.json`

- [ ] **Step 1: 写时间轴输入（人工维护、入库）**

`opstack-executor/tests/t8n/generator/matrix/input_schedule.json`：
```json
{
  "note": "Synthetic activation times for the 9 modeled EL forks; each row must be strictly increasing so a timestamp sweep lands exactly on each activation.",
  "forks": [
    {"name": "regolith", "time": 0},
    {"name": "canyon",   "time": 1000},
    {"name": "ecotone",  "time": 2000},
    {"name": "fjord",    "time": 3000},
    {"name": "granite",  "time": 4000},
    {"name": "holocene", "time": 5000},
    {"name": "isthmus",  "time": 6000},
    {"name": "jovian",   "time": 7000},
    {"name": "karst",    "time": 8000}
  ]
}
```

- [ ] **Step 2: 写 dumper（完整代码）**

```go
// Command opnodewin sweeps the 9 modeled forks over a synthetic schedule and asks the
// pinned op-node which Engine API method versions it selects at each activation
// timestamp. op-node is the sole authority for CL method selection (specs and op-geth
// do not describe it): rollup.Config.{ForkchoiceUpdatedVersion,NewPayloadVersion,GetPayloadVersion}.
package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"

	"github.com/ethereum-optimism/optimism/op-node/rollup"
	"github.com/ethereum-optimism/optimism/op-service/eth"
)

type forkIn struct {
	Name string `json:"name"`
	Time uint64 `json:"time"`
}
type scheduleIn struct {
	Forks []forkIn `json:"forks"`
}
type row struct {
	Fork               string `json:"fork"`
	Timestamp          uint64 `json:"timestamp"`
	NewPayload         string `json:"newPayload"`
	ForkchoiceUpdated  string `json:"forkchoiceUpdated"`
	GetPayload         string `json:"getPayload"`
}
type artifact struct {
	Pin         string `json:"pin"`
	GeneratedBy string `json:"generated_by"`
	Schedule    string `json:"schedule"`
	Rows        []row  `json:"windows"`
}

func timePtr(v uint64) *uint64 { return &v }

func main() {
	if len(os.Args) != 4 {
		fmt.Fprintln(os.Stderr, "usage: opnodewin <optimism-pin> <input-schedule.json> <out-dir>")
		os.Exit(2)
	}
	pin, inPath, outDir := os.Args[1], os.Args[2], os.Args[3]
	raw, err := os.ReadFile(inPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	var in scheduleIn
	if err := json.Unmarshal(raw, &in); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	byName := map[string]uint64{}
	for _, f := range in.Forks {
		byName[f.Name] = f.Time
	}
	cfg := &rollup.Config{
		RegolithTime: timePtr(byName["regolith"]),
		CanyonTime:   timePtr(byName["canyon"]),
		EcotoneTime:  timePtr(byName["ecotone"]),
		FjordTime:    timePtr(byName["fjord"]),
		GraniteTime:  timePtr(byName["granite"]),
		HoloceneTime: timePtr(byName["holocene"]),
		IsthmusTime:  timePtr(byName["isthmus"]),
		JovianTime:   timePtr(byName["jovian"]),
	}
	out := artifact{Pin: pin, GeneratedBy: "cmd/opnodewin", Schedule: "generator/matrix/input_schedule.json"}
	for _, f := range in.Forks {
		// Sweep exactly at the activation timestamp: Is<fork>(ts) is >= based.
		ts := f.Time
		attr := &eth.PayloadAttributes{Timestamp: eth.Uint64Quantity(ts)}
		out.Rows = append(out.Rows, row{
			Fork:              f.Name,
			Timestamp:         ts,
			NewPayload:        string(cfg.NewPayloadVersion(ts)),
			ForkchoiceUpdated: string(cfg.ForkchoiceUpdatedVersion(attr)),
			GetPayload:        string(cfg.GetPayloadVersion(ts)),
		})
	}
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	f, err := os.Create(filepath.Join(outDir, "engine_api_windows.json"))
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	defer f.Close()
	enc := json.NewEncoder(f)
	enc.SetIndent("", "  ")
	if err := enc.Encode(out); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
```

> 注：`karst` 不在 op-node `Config` 里（pin 无 `KarstTime`），因此 karst 行的期望值等同 jovian 行——这正是 §6 M1 要由 `known_deviations.json` 显式建模的偏离。

- [ ] **Step 3: 构建并运行（在 optimism pin 树内 build）**

```bash
cd ~/.cache/fisco-t8n-corpus
O=/Users/octopus/octo/code/optimism
T8N=opstack-executor/tests/t8n
rm -rf "$O/cmd/opt8n-opnodewin" && mkdir -p "$O/cmd/opt8n-opnodewin"
cp "$T8N/generator/cmd/opnodewin/main.go" "$O/cmd/opt8n-opnodewin/main.go"
( cd "$O" && go build -o /tmp/opt8n-opnodewin ./cmd/opt8n-opnodewin )   # 期望无输出
/tmp/opt8n-opnodewin 76e4fad54244ec6bd07dad07e42c82a16ab5113a \
  "$T8N/generator/matrix/input_schedule.json" "$T8N/matrix"
rm -rf "$O/cmd/opt8n-opnodewin"
python3 -m json.tool "$T8N/matrix/engine_api_windows.json" | head -30
```
期望（**已实测**）：9 行 `windows`，每行三个字段是 **完整 Engine 方法名**（`eth.EngineAPIMethod` 的值就是方法名，不是 `V2`）：

| fork | newPayload | forkchoiceUpdated | getPayload |
| --- | --- | --- | --- |
| regolith | engine_newPayloadV2 | engine_forkchoiceUpdatedV1 | engine_getPayloadV2 |
| canyon | engine_newPayloadV2 | engine_forkchoiceUpdatedV2 | engine_getPayloadV2 |
| ecotone/fjord/granite/holocene | engine_newPayloadV3 | engine_forkchoiceUpdatedV3 | engine_getPayloadV3 |
| isthmus/jovian/karst | engine_newPayloadV4 | engine_forkchoiceUpdatedV3 | engine_getPayloadV4 |

`karst` 与 `jovian` 相同（op-node 无 Karst/KarstTime）——这正是 `known_deviations.json` 要登记的 getPayload 偏离；本仓在 Karst 实现 `engine_getPayloadV5`。

- [ ] **Step 4: 提交**

```bash
cd ~/.cache/fisco-t8n-corpus
git add opstack-executor/tests/t8n/generator/matrix/input_schedule.json \
        opstack-executor/tests/t8n/generator/cmd/opnodewin/main.go
git commit -m "feat(generator): dump op-node engine api version windows for the matrix"
```

---

## Task 3: `known_deviations.json`（人工契约，`[corpus]`）

**Files:**
- Create: `opstack-executor/tests/t8n/matrix/known_deviations.json`（入库；不受 `.gitignore` 影响）

- [ ] **Step 1: 写入已记录的偏离**

```json
{
  "note": "Cells where FISCO intentionally diverges from the pin-derived expectation. Values are FULL engine method names (what the artifact carries). Every entry MUST cite the design decision log row; an entry whose cell no longer diverges makes the matrix fail (stale deviation).",
  "deviations": [
    {
      "fork": "karst",
      "method": "getPayload",
      "expected_from_pin": "engine_getPayloadV4",
      "implemented": "engine_getPayloadV5",
      "decision": "docs/2026-09-09-s3-engine-api-versions-design.md §4.2/§9: Karst adds getPayloadV5 (op-node at pin has no Karst/KarstTime, so it cannot adjudicate)"
    }
  ]
}
```

- [ ] **Step 2: 提交**

```bash
cd ~/.cache/fisco-t8n-corpus
git add opstack-executor/tests/t8n/matrix/known_deviations.json
git commit -m "feat(generator): record the karst getPayloadV5 window deviation"
```

---

## Task 4: 把两个 dumper 并入 `regen.sh` 仪式（`[corpus]`）

**Files:**
- Modify: `opstack-executor/tests/t8n/generator/regen.sh`

- [ ] **Step 1: 写失败的检查（扩展现有判据）**

在 `regen.sh` 的「验证判据」段**之前**插入一个 `matrix` 段，且让判据 ③ 覆盖新的契约文件。先在脚本末尾追加一个新判据并运行，确认它在 `matrix/` 缺失时失败：

```bash
cd ~/.cache/fisco-t8n-corpus
cat >> opstack-executor/tests/t8n/generator/regen.sh <<'EOF'

# ---- 判据 4：matrix 工件与契约（P1）----
MATRIX_DIR="$T8N_DIR/matrix"
[ -f "$MATRIX_DIR/caps.json" ]                 || { echo "matrix: caps.json missing" >&2; exit 1; }
[ -f "$MATRIX_DIR/engine_api_windows.json" ]   || { echo "matrix: engine_api_windows.json missing" >&2; exit 1; }
[ -f "$MATRIX_DIR/known_deviations.json" ]     || { echo "matrix: known_deviations.json missing" >&2; exit 1; }
EOF
rm -f opstack-executor/tests/t8n/matrix/caps.json
OPGETH=/Users/octopus/octo/code/blockchain-impl/op-geth \
  bash opstack-executor/tests/t8n/generator/regen.sh; echo "期望非 0，实际 rc=$?"
```
期望：脚本以非 0 退出并打印 `matrix: caps.json missing`。

- [ ] **Step 2: 让脚本自己生成两个工件（最小实现）**

**先**把变量提到脚本头部（`OPGETH`/`PIN` 附近，评审 F7：`set -u` 下 cleanup 早于定义会二次报错）：
```bash
OP_NODE_REPO="${OP_NODE_REPO:-/Users/octopus/octo/code/optimism}"
OP_NODE_PIN="${OP_NODE_PIN:-76e4fad54244ec6bd07dad07e42c82a16ab5113a}"
MATRIX_DIR="$T8N_DIR/matrix"
```
且在构建前断言 **optimism 引用树的清洁性**（评审 F10；只查 Go 相关子树即可，该树可被其他工作改动非 Go 文件）：
```bash
[ -z "$(git -C "$OP_NODE_REPO" status --porcelain -- op-node op-service)" ] || {
  echo "op-node/op-service worktree dirty; refusing to build the dumper" >&2; exit 1; }
```
然后在 `opt8n-ref` 构建段之后、判据段之前插入：

```bash
# ---- matrix 工件（P1）：两个 dumper 各自在对应 pin 树内 build ----
OP_NODE_REPO="${OP_NODE_REPO:-/Users/octopus/octo/code/optimism}"
OP_NODE_PIN="${OP_NODE_PIN:-76e4fad54244ec6bd07dad07e42c82a16ab5113a}"
MATRIX_DIR="$T8N_DIR/matrix"
mkdir -p "$MATRIX_DIR"

rm -rf "$OPGETH/cmd/opt8n-caps" && mkdir -p "$OPGETH/cmd/opt8n-caps"
cp "$GEN_DIR/cmd/caps/main.go" "$OPGETH/cmd/opt8n-caps/main.go"
( cd "$OPGETH" && go build -o "$SCRATCH-caps" ./cmd/opt8n-caps )
rm -rf "$OPGETH/cmd/opt8n-caps"
"$SCRATCH-caps" "$PIN" "$MATRIX_DIR"

[ "$(git -C "$OP_NODE_REPO" rev-parse HEAD)" = "$OP_NODE_PIN" ] || { echo "op-node HEAD != $OP_NODE_PIN" >&2; exit 1; }
rm -rf "$OP_NODE_REPO/cmd/opt8n-opnodewin" && mkdir -p "$OP_NODE_REPO/cmd/opt8n-opnodewin"
cp "$GEN_DIR/cmd/opnodewin/main.go" "$OP_NODE_REPO/cmd/opt8n-opnodewin/main.go"
( cd "$OP_NODE_REPO" && go build -o "$SCRATCH-opnodewin" ./cmd/opt8n-opnodewin )
rm -rf "$OP_NODE_REPO/cmd/opt8n-opnodewin"
"$SCRATCH-opnodewin" "$OP_NODE_PIN" "$GEN_DIR/matrix/input_schedule.json" "$MATRIX_DIR"
```

并把 `cleanup()` 的清理范围扩到**两个 pin 树里的所有临时物**（否则构建中途失败会在 pin 树里留残留，破坏 §1.2 的「引用树清洁性」纪律）：
```bash
cleanup() {
  rc=$?
  rm -rf "$SCRATCH"                          # op-geth/cmd/opt8n-ref*
  rm -f "$OPGETH/opt8n-ref" "$SCRATCH-caps" "$SCRATCH-opnodewin"
  rm -rf "$OPGETH/cmd/opt8n-caps" "$OP_NODE_REPO/cmd/opt8n-opnodewin"
  rmdir "$OP_NODE_REPO/cmd" 2>/dev/null || true   # optimism 原本无 cmd/，别留下空目录（评审 F10）
  exit $rc
}
```
`trap cleanup EXIT` 必须在**所有**临时目录创建之前安装（现有脚本已在构建段前安装，保持这一点）。
> **注意（评审 F2）**：不要在 regen.sh 的插入块里做「`opt8n-*` 残留检查」——此刻脚本自己的 `cmd/opt8n-ref`、`cmd/opt8n-ref-caps`、`cmd/opt8n-ref-opnodewin` 都还在（清理只在 EXIT trap），检查必然报错。残留校验放在**脚本退出之后**（Step 3 的收尾命令），或者只查本脚本会删掉的两个新目录。

判据 ③ 的契约文件清单追加 matrix 的三项（沿用脚本现有的**绝对** `$T8N_DIR` 变量——`$T8N_REL` 不存在，`set -u` 下会直接挂）：
```bash
git -C "$REPO_ROOT" diff --exit-code -- \
  "$T8N_DIR/vectors/manifest.txt" "$T8N_DIR/vectors"/*.md \
  "$T8N_DIR/golden/engine/manifest.txt" "$T8N_DIR/golden/engine/SHA256SUMS" \
  "$T8N_DIR/matrix/manifest.txt" "$T8N_DIR/matrix/SHA256SUMS" "$T8N_DIR/matrix/known_deviations.json"
```
（`caps.json`/`engine_api_windows.json` 是**生成物**，被 ignore，不进 diff 判据；`manifest.txt`/`SHA256SUMS` 由本脚本**幂等生成**，因此「regen 后 git diff 无变化」等价于「pin 未漂移」——与既有 vectors/golden 契约的做法一致。）

在同一个插入块里**幂等生成**这两份契约（禁止手改）：
```bash
python3 - "$MATRIX_DIR" <<'PYEOF'
import hashlib, pathlib, sys
d = pathlib.Path(sys.argv[1])
gen = ["caps.json", "engine_api_windows.json"]
lines = ["# matrix artifacts (generated by regen.sh; do not edit by hand)",
         "caps.json", "engine_api_windows.json", "known_deviations.json"]
(d / "manifest.txt").write_text("\n".join(lines) + "\n")
(d / "SHA256SUMS").write_text(
    "\n".join(f"{hashlib.sha256((d / n).read_bytes()).hexdigest()}  {n}" for n in gen) + "\n")
PYEOF
```

- [ ] **Step 3: 跑通并生成 matrix 契约（manifest/SHA256SUMS）**

```bash
cd ~/.cache/fisco-t8n-corpus
OPGETH=/Users/octopus/octo/code/blockchain-impl/op-geth \
  bash opstack-executor/tests/t8n/generator/regen.sh; echo "rc=$?"   # 期望 0
# 脚本退出后再查残留（此刻临时物应已被 EXIT trap 清掉）
for t in /Users/octopus/octo/code/blockchain-impl/op-geth /Users/octopus/octo/code/optimism; do
  n=$(git -C "$t" status --porcelain | grep -c 'opt8n-' || true)
  [ "$n" -eq 0 ] || { echo "residue in $t: $n entries" >&2; exit 1; }
done
cat opstack-executor/tests/t8n/matrix/SHA256SUMS      # 期望两行：caps.json / engine_api_windows.json
git diff --exit-code -- opstack-executor/tests/t8n/matrix/SHA256SUMS \
                         opstack-executor/tests/t8n/matrix/manifest.txt   # 期望无输出（契约 = 生成物，幂等）
```

- [ ] **Step 4: 提交**

```bash
cd ~/.cache/fisco-t8n-corpus
git add opstack-executor/tests/t8n/generator/regen.sh \
        opstack-executor/tests/t8n/matrix/SHA256SUMS \
        opstack-executor/tests/t8n/matrix/manifest.txt
git commit -m "feat(generator): build the matrix artifacts in the regen ritual"
```

---

## Task 5: M1 矩阵测试（`[fisco]`）

**Files:**
- Create: `engine/test/unittests/engine/OpEngineApiMatrixTest.cpp`
- Modify: `engine/test/CMakeLists.txt`

- [ ] **Step 1: 加编译定义**

`engine/test/CMakeLists.txt`，在 `target_include_directories(...)` 之后插入：
```cmake
target_compile_definitions(${TEST_BINARY_NAME} PRIVATE
    OP_MATRIX_DIR="${CMAKE_SOURCE_DIR}/opstack-executor/tests/t8n/matrix")
```
（路径经 symlink 解析到语料库仓；缺工件时测试 skip，见 Step 3。）

- [ ] **Step 2: 写失败的测试（完整代码）**

```cpp
// M1: every (fork, method) window must equal the pinned op-node's selection, except
// for cells recorded in matrix/known_deviations.json. The artifact is generated by
// the corpus regen ritual (op-stack-e2e-tests); its absence skips rather than fails.
#include "bcos-framework/engine/OpForkId.h"
#include "bcos-framework/engine/Types.h"           // ApiVersion V1=1..V5=5
#include <bcos-evm/opstack/OpForkSchedule.h>       // parse()/forkAt(): name -> OpFork
#include <opstack-executor/OpSchedulerSeam.h>      // bcos::evm::engine::detail::tryEngineForkId
#include <engine/bcos-engine/OpEngineService.h>    // bcos::engine::engine_common::op::supportedOpCapabilities
#include <boost/test/unit_test.hpp>
#include <fstream>
#include <set>
#include <fmt/format.h>
#include <json/json.h>
#include <optional>
#include <string>
#include <vector>

namespace
{
constexpr std::string_view c_matrixDir = OP_MATRIX_DIR;

std::optional<Json::Value> loadJson(std::string const& name)
{
    std::ifstream in(std::string(c_matrixDir) + "/" + name);
    if (!in)
    {
        return std::nullopt;
    }
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, in, &root, &errors))
    {
        BOOST_FAIL("cannot parse " + name + ": " + errors);
    }
    return root;
}

/// The artifact carries op-node's eth.EngineAPIMethod values, which ARE the full engine
/// method names (e.g. "engine_newPayloadV2"); this lane's profile holds ApiVersion V1=1..V5=5,
/// so the comparison renders the same naming rule op-geth uses (engine_ + family + version).
std::string methodName(bcos::engine::OpForkId fork, std::string_view method)
{
    auto const profile = bcos::engine::engineApiProfileFor(fork);
    std::uint32_t version = 0;
    if (method == "newPayload")
    {
        version = static_cast<std::uint32_t>(profile.newPayload);
    }
    else if (method == "forkchoiceUpdated")
    {
        version = static_cast<std::uint32_t>(profile.forkchoiceUpdated);
    }
    else
    {
        version = static_cast<std::uint32_t>(profile.getPayload);
    }
    return "engine_" + std::string(method) + "V" + std::to_string(version);
}

bool isRecordedDeviation(Json::Value const& deviations, std::string const& fork,
    std::string const& method, std::string const& expected, std::string const& implemented)
{
    for (auto const& d : deviations)   // 调用方必须传 (*deviations)["deviations"]，见下
    {
        if (d["fork"].asString() == fork && d["method"].asString() == method &&
            d["expected_from_pin"].asString() == expected &&
            d["implemented"].asString() == implemented)
        {
            return true;
        }
    }
    return false;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpEngineApiMatrixSuite)

BOOST_AUTO_TEST_CASE(ForkMethodWindowsMatchPinnedOpNode)
{
    auto windows = loadJson("engine_api_windows.json");
    auto deviations = loadJson("known_deviations.json");
    auto caps = loadJson("caps.json");
    if (!windows || !deviations || !caps)
    {
        BOOST_TEST_MESSAGE("matrix artifacts absent; run the corpus regen ritual -- skipping");
        return;
    }
    std::size_t checked = 0;
    for (auto const& row : (*windows)["windows"])
    {
        auto const fork = row["fork"].asString();
        // name -> OpFork via the public schedule parser, then OpFork -> OpForkId via the
        // seam's mapping (there is no name->OpForkId helper; the engine enum is renumbered).
        auto const id = bcos::evm::engine::detail::tryEngineForkId(
            bcos::evm::opstack::OpForkSchedule::parse("0:" + fork).forkAt(0));
        BOOST_REQUIRE_MESSAGE(
            id.has_value(), "artifact carries a fork the engine does not model: " + fork);
        for (auto const method : {"newPayload", "forkchoiceUpdated", "getPayload"})
        {
            auto const expected = row[method].asString();
            auto const implemented = methodName(*id, method);
            if (expected == implemented)
            {
                ++checked;
                continue;
            }
            // 注意实参顺序：helper 的签名是 (deviations, fork, method, expected, implemented)。
            BOOST_CHECK_MESSAGE(
                isRecordedDeviation((*deviations)["deviations"], fork, method, expected, implemented),
                fmt::format("unrecorded window divergence {}/{}: pin={} vs implemented={}", fork,
                    method, expected, implemented));
        }
    }
    BOOST_CHECK_GT(checked, 0U);
    // The advertised caps must cover the union of the windows (same naming rule as
    // op-geth, whose list is the caps.json artifact); anything advertised outside the
    // three families must at least exist in op-geth's list.
    auto const advertised = bcos::engine::engine_common::op::supportedOpCapabilities();
    std::set<std::string> advertisedSet(advertised.begin(), advertised.end());
    std::set<std::string> opGethCaps;
    for (auto const& c : (*caps)["caps"])
    {
        opGethCaps.insert(c.asString());
    }
    std::set<std::string> derived;
    for (auto const& row : (*windows)["windows"])
    {
        derived.insert(row["newPayload"].asString());
        derived.insert(row["forkchoiceUpdated"].asString());
        derived.insert(row["getPayload"].asString());
    }
    for (auto const& cap : derived)
    {
        BOOST_CHECK_MESSAGE(advertisedSet.count(cap) != 0, "window method not advertised: " + cap);
        BOOST_CHECK_MESSAGE(opGethCaps.count(cap) != 0, "method absent upstream: " + cap);
    }
    for (auto const& cap : advertisedSet)
    {
        if (cap.rfind("engine_newPayload", 0) == 0 || cap.rfind("engine_forkchoiceUpdated", 0) == 0 ||
            cap.rfind("engine_getPayload", 0) == 0)
        {
            // A recorded deviation explains an advertised method outside the window
            // union (karst getPayloadV5 is exactly that), so exempt it explicitly.
            bool deviated = false;
            for (auto const& d : (*deviations)["deviations"])
            {
                deviated = deviated || (d["implemented"].asString() == cap);
            }
            BOOST_CHECK_MESSAGE(derived.count(cap) != 0 || deviated,
                "advertised method is outside every fork window: " + cap);
        }
    }
    BOOST_TEST_MESSAGE("matrix cells checked: " << checked);
}

BOOST_AUTO_TEST_SUITE_END()
```

> **实现者注意（已核实）**：`supportedOpCapabilities()` 声明于 `engine/bcos-engine/OpEngineService.h:81-83`，完整限定名是 **`bcos::engine::engine_common::op::supportedOpCapabilities()`**（相对形式 `engine_common::op::…` 只在已展开 `bcos::engine` 的上下文中成立）（先例：`engine/test/unittests/engine/OpEngineReviewFixTest.cpp:203`）；若想断言"节点实际 advertise 的集合"而非函数返回值，可改用 RPC seam：`bcos::task::syncWait(pair.service.exchangeCapabilities({}))`（先例：`OpEngineServiceParityTest.cpp:94`）。另外 `isRecordedDeviation` 的调用参数顺序需按实况修到能编译——**先跑 Step 3 让它红/编译失败，再修**。`bcos::engine::forkFromName` 若返回 `OpFork` 而非 optional，改成 `BOOST_REQUIRE` + 直接比较。

- [ ] **Step 3: 重配 cmake 并确认失败/缺口**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
cmake -B build -S . >/dev/null 2>&1     # GLOB 需重配才会纳入新文件
ninja -C build test-bcos-engine 2>&1 | tail -5
build/engine/test/test-bcos-engine --run_test=OpEngineApiMatrixSuite 2>&1 | tail -8
```
期望：先编译失败或断言红（记录报错作为 RED 证据）。

- [ ] **Step 4: 修到绿**

```bash
ninja -C build test-bcos-engine && build/engine/test/test-bcos-engine --run_test=OpEngineApiMatrixSuite 2>&1 | tail -4
```
期望：`*** No errors detected`，且消息里打印出 `matrix cells checked: N`（N>0）。

- [ ] **Step 4b: 补 M1 的负向格与 V1 缺席断言（spec §6 M1/§9 P1 要求）**

在 `OpEngineApiMatrixSuite` 内新增一个用例，覆盖两类负向：
```cpp
/// M1 negative cells: a window-foreign version must be rejected (-38005 class), and the
/// pre-Bedrock payload methods must not be advertised at all.
BOOST_AUTO_TEST_CASE(WindowForeignVersionIsRejectedAndV1IsAbsent)
{
    auto caps = loadJson("caps.json");
    if (!caps)
    {
        BOOST_TEST_MESSAGE("caps artifact absent; skipping");
        return;
    }
    auto const advertised = bcos::engine::engine_common::op::supportedOpCapabilities();
    // V1: op-node never selects newPayloadV1/getPayloadV1 (Bedrock is its first fork).
    BOOST_CHECK(std::find(advertised.begin(), advertised.end(), "engine_newPayloadV1") == advertised.end());
    BOOST_CHECK(std::find(advertised.begin(), advertised.end(), "engine_getPayloadV1") == advertised.end());
    // FCU V4 must not be advertised either (upstream has it; this lane answers -38005).
    BOOST_CHECK(std::find(advertised.begin(), advertised.end(), "engine_forkchoiceUpdatedV4") == advertised.end());
    // Every advertised method must exist in op-geth's reflective list (naming-rule guard)
    // -- except engine_exchangeCapabilities: op-geth's own reflection SKIPS the method
    // itself (eth/catalyst/api.go:934-945, `if name == "ExchangeCapabilities" { continue }`),
    // while this lane advertises it. Measured: op-geth's list has 36 entries and does not
    // contain engine_exchangeCapabilities.
    Json::Value const& gethCaps = (*caps)["caps"];
    for (auto const& cap : advertised)
    {
        if (cap.rfind("engine_", 0) != 0 || cap == "engine_exchangeCapabilities")
        {
            continue;
        }
        bool present = false;
        for (auto const& c : gethCaps)
        {
            present = present || (c.asString() == cap);
        }
        BOOST_CHECK_MESSAGE(present, "advertised method absent upstream: " + cap);
    }
}
```
真机上的 -38005 驱动（对某 fork 用窗口外版本发一次 newPayload 并断言错误类）放在本用例的下半段：用 `ImportServiceFixture` + `f.service.newPayload(req, /*version=*/错版本)`，断言返回的 payload 状态为 Invalid 且错误消息含 `-38005`（已有 `OpEngineApiVersionsTest` 可作范本）。若该驱动在 fixture 上不可行，如实报告并只保留 cap/集合层断言。

- [ ] **Step 5: 回归既有套件**

```bash
for t in engine/test/test-bcos-engine opstack-executor/tests/opstack-executor-block-tests; do
  out=$(build/$t 2>&1); printf "%-40s rc=%s %s\n" "$t" "$?" "$(echo "$out" | grep -oE 'Running [0-9]+ test cases')"
done
```
期望：rc=0，engine 用例数 = 294 + 新用例数。

- [ ] **Step 6: 提交**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
git add engine/test/CMakeLists.txt engine/test/unittests/engine/OpEngineApiMatrixTest.cpp
git commit -m "test(engine): assert the fork x method matrix against the pinned op-node artifact"
```

---

## Task 6: 不变量库 `SequenceInvariants.h`（`[fisco]`）

**Files:**
- Create: `engine/test/unittests/engine/support/SequenceInvariants.h`

- [ ] **Step 1: 写断言库（完整代码）**

```cpp
// I1-I6: the canonicality invariants every import/FCU sequence must preserve after
// every step. Derived from the S5+S6 design §4.2/§4.4.x and the defects found by the
// milestone review (N1/N2/N3/NEW-1/NEW-2/NEW-3); these are the "guarding格" of the
// M4 sequence matrix.
#pragma once

#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-ledger/bcos-ledger/LedgerMethods.h>
#include <boost/test/unit_test.hpp>
#include <bcos-task/Wait.h>
#include <fmt/format.h>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace op_matrix
{
/// I1: number->hash and hash->number agree for every canonical height. (N2/NEW-1)
template <class StorageType>
void checkNumberHashAgreement(StorageType& storage, bcos::protocol::BlockNumber head)
{
    auto view = storage.forkCommitted();
    for (bcos::protocol::BlockNumber n = 0; n <= head; ++n)
    {
        auto hash = bcos::task::syncWait(
            bcos::ledger::getBlockHash(view, n, bcos::ledger::fromStorage));
        BOOST_REQUIRE_MESSAGE(hash.has_value(), fmt::format("I1: no hash at height {}", n));
        auto number = bcos::task::syncWait(
            bcos::ledger::getBlockNumber(view, *hash, bcos::ledger::fromStorage));
        BOOST_REQUIRE_MESSAGE(number.has_value(), fmt::format("I1: hash at {} unresolvable", n));
        BOOST_CHECK_EQUAL(*number, n);
    }
}

/// I2: the by-number transaction list is resolvable and has the head's tx count. (N1)
/// NOTE: the storage overload of ledger::getBlockData takes the blockFactory as its 4th
/// argument (LedgerMethods.h) -- see the existing CanonicalImportedBlockHasNumberToTxsRow call.
template <class StorageType>
void checkByNumberTransactions(StorageType& storage, bcos::protocol::BlockFactory& blockFactory,
    bcos::protocol::BlockNumber head, std::size_t expectedTxs)
{
    auto view = storage.forkCommitted();
    auto block = bcos::task::syncWait(bcos::ledger::getBlockData(
        view, head, bcos::ledger::HEADER | bcos::ledger::TRANSACTIONS, blockFactory));
    BOOST_REQUIRE(block != nullptr);
    BOOST_CHECK_EQUAL(block->transactionsSize(), expectedTxs);
}

/// I3: nothing above the head resolves by number. (NEW-1/N2)
template <class StorageType>
void checkNoRowsAboveHead(StorageType& storage, bcos::protocol::BlockNumber head)
{
    auto view = storage.forkCommitted();
    for (bcos::protocol::BlockNumber n = head + 1; n <= head + 3; ++n)
    {
        BOOST_CHECK_MESSAGE(!bcos::task::syncWait(
                                bcos::ledger::getBlockHash(view, n, bcos::ledger::fromStorage))
                                .has_value(),
            fmt::format("I3: height {} still resolves above head {}", n, head));
    }
}

/// I4: hashes that were de-canonicalized must not resolve any more. (NEW-1/NEW-2)
template <class StorageType>
void checkHashesUnresolvable(StorageType& storage, std::vector<bcos::h256> const& deadHashes)
{
    auto view = storage.forkCommitted();
    for (auto const& h : deadHashes)
    {
        BOOST_CHECK_MESSAGE(!bcos::task::syncWait(
                                bcos::ledger::getBlockNumber(view, h, bcos::ledger::fromStorage))
                                .has_value(),
            fmt::format("I4: de-canonicalized hash {} still resolves", h.hex()));
    }
}

/// I5: the committed tip pointer equals the announced head. (post-condition proxy:
/// the real state-root check runs inside canonicalize and throws on mismatch.)
template <class StorageType>
void checkTipIs(StorageType& storage, bcos::protocol::BlockNumber expectedTip)
{
    auto view = storage.forkCommitted();
    auto const tip = bcos::task::syncWait(
        bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage));
    BOOST_CHECK_EQUAL(tip, static_cast<int64_t>(expectedTip));
}

/// I6: the tip never moves backwards unless the scenario says so.
inline void checkTipMonotonic(
    bcos::protocol::BlockNumber previous, bcos::protocol::BlockNumber current, bool allowRewind)
{
    if (!allowRewind)
    {
        BOOST_CHECK_MESSAGE(current >= previous,
            fmt::format("I6: tip rewound {} -> {}", previous, current));
    }
}

/// What a single sequence step must satisfy; every sequence builds one of these after
/// each step and calls checkAll, so I1-I6 really run everywhere (spec §9 P1).
struct StepExpectations
{
    bcos::protocol::BlockNumber previousTip{};
    bcos::protocol::BlockNumber head{};
    std::size_t headTxCount{};
    std::vector<bcos::h256> deadHashes{};  // I4: hashes de-canonicalized by this step
    bool allowRewind = false;              // I6: true only for S5/S14-style rewinds
};

template <class StorageType>
void checkAll(StorageType& storage, bcos::protocol::BlockFactory& blockFactory,
    StepExpectations const& expectations)
{
    checkTipMonotonic(expectations.previousTip, expectations.head, expectations.allowRewind);
    checkTipIs(storage, expectations.head);
    checkNumberHashAgreement(storage, expectations.head);
    checkByNumberTransactions(storage, blockFactory, expectations.head, expectations.headTxCount);
    checkNoRowsAboveHead(storage, expectations.head);
    if (!expectations.deadHashes.empty())
    {
        checkHashesUnresolvable(storage, expectations.deadHashes);
    }
}
}  // namespace op_matrix
```

- [ ] **Step 2: 编译（空跑）**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
cmake -B build -S . >/dev/null 2>&1
ninja -C build test-bcos-engine 2>&1 | grep -E "error:|FAILED" | head -5 || echo "compiles (header not yet included)"
```

- [ ] **Step 3: 提交**

```bash
git add engine/test/unittests/engine/support/SequenceInvariants.h
git commit -m "test(engine): add the I1-I6 canonicality invariant battery"
```

---

## Task 7a: 把导入夹具抽到共享头（纯重构，`[fisco]`）

**Files:**
- Modify: `engine/test/unittests/engine/support/OpEngineKarstTestHarness.h`（承接符号）
- Modify: `engine/test/unittests/engine/OpEngineImportFcuTest.cpp`（改为 include）

**背景（已核实，含独立评审的清单）**：`OpEngineImportFcuTest.cpp` 的夹具**位于第二个匿名 namespace 内（216–606 行）**，跨 TU 无法复用。被迁移的符号共 **11 个**（行号为当前 HEAD）：`makeImportReceiptFactory`(:54)、`kImportEip1559Sender`(:52)、`buildImportDepositTx`(:78)、`makeImportDeposit`(:59)、`makeImportHeader`(:94)、`seedCommittedGenesis`(:122)、`ImportSchedulerFixture`(:177)、`DeltaStrippingScheduler`(:223)、`makeImportDelegate`(:255)、`BlockingGate`(:276)、`makeBlockingVerifyDelegate`(:324)，外加 `ImportServiceFixtureT`(:343)、别名 `ImportServiceFixture`/`CacheImportServiceFixture`(:604-605) 与辅助（`makeTestStorage`、`committedTipNumber`、`committedHashAt` 等）。
`fixtureHeadHash()` 已存在于 `support/OpEngineKarstTestHarness.h:694`——**不要重复定义**。

- [ ] **Step 1: 抽取到具名 namespace**：把上述符号整体移入 `support/OpEngineKarstTestHarness.h` 的具名 namespace（建议 `op_engine_test`），保留依赖顺序；`OpEngineImportFcuTest.cpp` 删除该段、改为 `#include` + `using namespace op_engine_test;`（或限定名）。
  **Unity/ODR 要求（评审 F-9）**：`OpEngineImportFcuTest.cpp` 与新测试文件都**不在** `engine/test/CMakeLists.txt` 的 `SKIP_UNITY_BUILD_INCLUSION` 名单里，两个 .cpp 可能被编进同一个 unity TU。因此迁移后的**非模板函数必须 `inline`**（或 `static`），类/结构体在头文件内定义（ODR 允许），否则会出现重复定义或符号冲突；`constexpr` 全局量用 `inline constexpr`。

- [ ] **Step 2: 编译并跑既有 30 例**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
cmake -B build -S . >/dev/null 2>&1
ninja -C build test-bcos-engine 2>&1 | grep -E "error:|redefinition|FAILED" | head -5 || true
build/engine/test/test-bcos-engine --run_test=OpEngineImportFcuTest 2>&1 | grep -E "Running|No errors|failure" | tail -3
```
期望：`Running 30 test cases... *** No errors detected`，且无 `redefinition`/`multiple definition` 报错。
（用例数用 `--list_content` 复核更稳：`build/engine/test/test-bcos-engine --list_content 2>&1 | grep -cE '^    [A-Za-z_][A-Za-z0-9_]*\*$'` → 期望 294。）

- [ ] **Step 3: 提交**

```bash
git add engine/test/unittests/engine/support/OpEngineKarstTestHarness.h         engine/test/unittests/engine/OpEngineImportFcuTest.cpp
git commit -m "test(engine): move the import fixture into the shared harness header"
```

---

## Task 7b: M4 的 14 条序列（`[fisco]`）

**Files:**
- Create: `engine/test/unittests/engine/OpEngineSequenceMatrixTest.cpp`

- [ ] **Step 1: 写前 3 条序列（S1–S3）+ 不变量调用（完整代码）**

```cpp
// M4: the import/FCU sequence matrix. Each scenario runs the I1-I6 battery after every
// step; the guarding findings are recorded in the suite comment per sequence so a
// future reader can trace a cell back to the defect it protects.
#include "bcos-framework/engine/OpForkId.h"
#include "support/OpEngineKarstTestHarness.h"
#include "support/SequenceInvariants.h"
#include <boost/test/unit_test.hpp>

using namespace bcos::engine;
using namespace op_engine_test;   // Task 7a 抽出的夹具命名空间
namespace op_matrix
{

BOOST_AUTO_TEST_SUITE(OpEngineSequenceMatrixSuite)

/// S1 (guards N1 via I2): forward-canonicalize three blocks, then assert the invariants.
BOOST_AUTO_TEST_CASE(S1_ForwardCanonicalize)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    checkAll(f.storage, *f.blockFactory, {.previousTip = 3, .head = 3, .headTxCount = 1});
}

/// S3 (guards N2/NEW-1): replace the canonical block at height 2 with a sibling.
BOOST_AUTO_TEST_CASE(S3_SameHeightSwitchDropsSibling)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    auto const bHash = f.seededChainHash[2];
    auto requestBPrime = f.makeSiblingAtHeight2();
    BOOST_REQUIRE_EQUAL(static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestBPrime, 4)).status),
        static_cast<int>(PayloadValidationStatus::Valid));
    ForkchoiceState fcu{requestBPrime.executionPayload.blockHash, requestBPrime.executionPayload.blockHash,
        fixtureHeadHash()};
    BOOST_REQUIRE_EQUAL(static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcu, nullptr, 3)).payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));
    checkAll(f.storage, *f.blockFactory,
        {.previousTip = 3, .head = 2, .headTxCount = 1,
            .deadHashes = {bHash, f.seededChainHash[3]}});
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace op_matrix
```

> 实现者注意：夹具已由 **Task 7a** 移到 `support/OpEngineKarstTestHarness.h` 的 `op_engine_test` 命名空间（含 `ImportServiceFixture`、`seededChainHash`、`validRequest`）。`makeSiblingAtHeight2()` 是给 fixture 加的小助手，实现 = 现有 `SwitchDropsReplacedSameHeightSiblingLedgerRows` 里那段 B′ 构造（timestamp+3000 与 `rebuildOpEthHeader` 重算 hash）。

- [ ] **Step 2: 重配 + 跑，确认红/缺口**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
cmake -B build -S . >/dev/null 2>&1
ninja -C build test-bcos-engine 2>&1 | tail -5
build/engine/test/test-bcos-engine --run_test='OpEngineSequenceMatrixSuite/*' 2>&1 | tail -8
```

- [ ] **Step 3: 补齐 S2、S4–S14（逐条，每条都跑一次）**

按 §6 M4 的守格表逐条实现；每条序列的结构与 S3 相同（驱动 → 每步构造 `StepExpectations` → `checkAll`）。**每条序列必须维护 `previousTip`**（进入前记录、每步更新），**S5/S14 一类显式回退场景置 `allowRewind = true`**，其余一律 false——这是 I6 能真正生效的前提（评审 F-12）。必须包含：
- S4 多块孤儿（A-B-C-D → B′@2 → 在 3 导入 B′ 子块，期望 Valid）；
- S9 `DeltaStrippingScheduler` 注入中链失败（沿用 `CanonicalizeRollsBackOnMidChainMergeFailure` 的构造），断言失败后 `checkNumberHashAgreement` 与冻结的 cache 快照不变；
- S10 `BlockingGate` 并发（沿用 `CanonicalizeHoldsNoLockAcrossAwait`）；
- **S13** 用 `CacheImportServiceFixture`：先前推 B1..B3 暖 cache，再同高切换到 B′@2，断言 Valid + `checkNumberHashAgreement` + 原始 cache 行（`NUMBER_2_HASH[2]`、`SYS_CURRENT_STATE`）；
- **S14** back-reorg：A-B-C → B′@2 → FCU 回 B@2（或 C@3），断言 Valid + `checkHashesUnresolvable({B′})` + tip 回退（`checkTipMonotonic(..., allowRewind=true)`）。

每加 1–2 条序列跑一次 `--run_test='OpEngineSequenceMatrixSuite/*'` 并报告用例数。

- [ ] **Step 4: 加随机序列（env 门控，默认跳过）**

```cpp
/// Randomized sequences: default 0 steps so the PR gate skips it; nightly sets
/// OPSEQUENCE_MATRIX_STEPS=100000 and OPSEQUENCE_MATRIX_SEED=<uint>.
BOOST_AUTO_TEST_CASE(S15_RandomizedSequences)
{
    auto const steps = std::getenv("OPSEQUENCE_MATRIX_STEPS");
    if (steps == nullptr)
    {
        BOOST_TEST_MESSAGE("OPSEQUENCE_MATRIX_STEPS unset; skipping randomized sequences");
        return;
    }
    auto const seed = std::getenv("OPSEQUENCE_MATRIX_SEED") ? std::stoull(std::getenv("OPSEQUENCE_MATRIX_SEED")) : 20260911ULL;
    std::mt19937_64 rng(seed);
    // 每步随机选：newPayload(新块) / FCU(当前或更早的已导入块) / 触发一次 finalize 推进，
    // 然后跑 checkNumberHashAgreement / checkNoRowsAboveHead（I1/I3）——只断言不变量。
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    auto head = 3;
    // driveRandomStep(rng, head) 的实现（放在 fixture 里，只用公开 API）：
    //   std::uniform_int_distribution<int> pick(0, 2);
    //   switch (pick(rng)) {
    //     case 0: {  // 新块：在 head 上造时间戳 +1000 的 block，newPayload + FCU
    //       auto req = validRequest(currentHeadHash(), head + 1);
    //       req.executionPayload.timestamp += 1000 * (head + 1);
    //       recomputeBlockHash(req);                  // 见现有测试的 rebuildOpEthHeader 手法
    //       if (newPayload(req, 4).status == Valid) { fcu(req.hash); head += 1; }
    //       break; }
    //     case 1: {  // 回退 FCU 到一个已导入的更早块（不改 head 的期望值）
    //       fcu(HASH_2_NUMBER 里随机取一个已导入 hash);
    //       break; }
    //     case 2: {  // finalize 推进：FCU 时把 safe/finalized 指向当前 head
    //       fcu(currentHeadHash(), /*safe=*/currentHeadHash(), /*finalized=*/currentHeadHash());
    //       break; }
    //   }
    // 断言只跑 I1/I3；head 只在 case 0 成功时 +1，保证不变量成立。
    for (int i = 0; i < std::stoi(steps); ++i)
    {
        f.driveRandomStep(rng, head);
        checkNumberHashAgreement(f.storage, head);
        checkNoRowsAboveHead(f.storage, head);
    }
}
```

- [ ] **Step 5: 全量回归 + 提交**

```bash
for t in engine/test/test-bcos-engine opstack-executor/tests/opstack-executor-block-tests; do
  out=$(build/$t 2>&1); printf "%-40s rc=%s %s\n" "$t" "$?" "$(echo "$out" | grep -oE 'Running [0-9]+ test cases')"
done
git add engine/test/unittests/engine/support/SequenceInvariants.h \
        engine/test/unittests/engine/support/OpEngineKarstTestHarness.h \
        engine/test/unittests/engine/OpEngineSequenceMatrixTest.cpp \
        engine/test/unittests/engine/OpEngineImportFcuTest.cpp
git commit -m "test(engine): add the 14-sequence import/FCU matrix with invariants I1-I6"
```
期望：rc=0；`OpEngineSequenceMatrixSuite` 报告 15 个用例（S1–S14 + 门控随机）。

---

## Task 8: 变异 harness 骨架 + 三个变体（`[fisco]`）

**Files:**
- Create: `tools/mutation/run.sh`, `tools/mutation/make-variant.sh`, `tools/mutation/variants/mapping.json`

- [ ] **Step 1: 变体的产生方式（手工最小逆向，不用整份 `git apply -R`）**

评审已实测：`git show <sha> -- <path>` 的整份 diff **只有 `4a59505a1` 能干净逆向**，`7efabc36e` 与 `d7e82a984` 会 `patch does not apply`（同区域被后续提交改过），且它们的 diff 同时含多条 finding。因此变体是**手工编写的最小逆向 patch**，流程：

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
# 1) 先看该 finding 的修复长什么样（定位核心判定）
git show 7efabc36e -- engine/bcos-engine/OpEngineService.inl | grep -n "metadata\|isLedgerCanonicalMetadataTable" | head
# 2) 在 HEAD 上手改，只反转「该 finding 的核心判定」（见下表；不要整段回退）
# 3) 跑一次 run.sh（它会 apply/红/还原），确认 RED 且归因成立
# 4) 保存并还原：
git diff > tools/mutation/variants/N2.patch && git checkout -- engine/bcos-engine/OpEngineService.inl
```

三个 P1 种子要反转的**核心判定**（其余改动保持不动）：

| 变体 | 文件 | 反转什么（意图） |
| --- | --- | --- |
| N1 | `engine/bcos-engine/OpEngineService.inl` | 去掉前推分支里 `SYS_NUMBER_2_TXS[number]` 的写入（`encodeNumberToTxsRow` 那次 `writeOne`）→ 恢复「按号取交易列表为空」 |
| N2 | `engine/bcos-engine/OpEngineService.inl` | 让 switch 的 step(1) 扫描**不再跳过** ledger 元数据表（即恢复「整面删除」）→ 祖先的 `HASH_2_NUMBER` 被抹掉 |
| NEW-3 | `engine/bcos-engine/OpEngineService.inl` | 把 switch 的单点 `co_await m_globalStateStorage.mergeToBackends(*switchDelta);` 换成把同一批行直接写 `m_globalStateStorage.m_latestBackend`（绕过 cache）→ 恢复「暖 cache 遮蔽新平面」 |

- [ ] **Step 2: 写运行器（完整代码）**

`tools/mutation/run.sh`：
```bash
#!/usr/bin/env bash
# Apply each reverse-fix variant, build the mapped target, run the mapped case, and
# REQUIRE it to fail -- a variant that leaves the matrix green is a blind spot.
# Red is decided by the process exit code AND the failure marker: Boost prints
# "*** No errors detected" on success, so a bare "errors detected" substring test would
# report every green run as red.
# Usage: run.sh [finding-id ...]   (default: every variant in mapping.json)
set -uo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
map="$root/tools/mutation/variants/mapping.json"

read_ids() { python3 -c "import json;print('\n'.join(v['variant'] for v in json.load(open('$map'))))"; }
field() { python3 -c "import json;m=json.load(open('$map'));print(next(v['$2'] for v in m if v['variant']=='$1'))"; }

ids=("$@")
if [ "${#ids[@]}" -eq 0 ]; then
  # Portable read loop: stock macOS bash 3.2 has no mapfile.
  while IFS= read -r id; do [ -n "$id" ] && ids+=("$id"); done < <(read_ids)
fi
if [ "${#ids[@]}" -eq 0 ]; then echo "no variants selected (empty mapping?)" >&2; exit 1; fi

is_red() {  # $1 = output, $2 = exit code
  [ "$2" -ne 0 ] && return 0
  echo "$1" | grep -qE '\*\*\* [0-9]+ failures? (is|are) detected|\*\*\* Errors were detected'
}

rc_all=0
for id in "${ids[@]}"; do
  patch="$root/tools/mutation/variants/$id.patch"
  [ -f "$patch" ] || { echo "[$id] NO VARIANT PATCH ($patch)"; rc_all=1; continue; }
  target=$(field "$id" target); bin=$(field "$id" binary)
  filter=$(field "$id" filter); also=$(field "$id" also_green 2>/dev/null || true)
  git -C "$root" apply --3way "$patch" || { echo "[$id] APPLY FAILED"; rc_all=1; continue; }
  # Restore on ANY exit (interrupt included) so the tree never stays patched.
  trap 'git -C "'"$root"'" checkout -- engine/bcos-engine/OpEngineService.inl 2>/dev/null || true' EXIT INT TERM
  if ! ninja -C "$root/build" "$target" >/dev/null 2>&1; then
    echo "[$id] BUILD FAILED under the variant (variant is unusable)"; rc_all=1
    git -C "$root" checkout -- engine/bcos-engine/OpEngineService.inl; trap - EXIT INT TERM; continue
  fi
  out="$("$root/build/$bin" "--run_test=$filter" 2>&1)"; code=$?
  if is_red "$out" "$code"; then
    echo "[$id] RED as required"
  else
    echo "[$id] STILL GREEN -- matrix blind spot"; rc_all=1
  fi
  if [ -n "$also" ]; then
    other="$("$root/build/$bin" "--run_test=$also" 2>&1)"; ocode=$?
    if is_red "$other" "$ocode"; then
      echo "[$id] NOT ATTRIBUTED -- other sequences also failed"; rc_all=1
    fi
  fi
  git -C "$root" checkout -- engine/bcos-engine/OpEngineService.inl
  trap - EXIT INT TERM
done
if [ -n "$(git -C "$root" status --porcelain -- engine/bcos-engine/OpEngineService.inl)" ]; then
  echo "variant file still modified after run" >&2; rc_all=1
fi
exit $rc_all
```

- [ ] **Step 3: 写 mapping（先三个变体）**

`tools/mutation/variants/mapping.json`：
```json
[
  {"variant": "N1", "finding": "N1", "target": "test-bcos-engine", "binary": "engine/test/test-bcos-engine", "filter": "OpEngineSequenceMatrixSuite/S1_ForwardCanonicalize", "also_green": "OpEngineSequenceMatrixSuite/*", "expect": "only_mapped_red"},
  {"variant": "N2", "finding": "N2", "target": "test-bcos-engine", "binary": "engine/test/test-bcos-engine", "filter": "OpEngineSequenceMatrixSuite/S3_SameHeightSwitchDropsSibling", "also_green": "OpEngineSequenceMatrixSuite/*", "expect": "only_mapped_red"},
  {"variant": "NEW-3", "finding": "NEW-3", "target": "test-bcos-engine", "binary": "engine/test/test-bcos-engine", "filter": "OpEngineSequenceMatrixSuite/S13_*", "also_green": "OpEngineSequenceMatrixSuite/*", "expect": "only_mapped_red"}
]
```
（`also_green` 用 `*` 会把 `filter` 自己也算进来——run.sh 的归因步因此会立刻报 `NOT ATTRIBUTED`。实现时把 `also_green` 写成**除该 filter 之外的**显式列表，例如 N1 的 `also_green` = `"OpEngineSequenceMatrixSuite/S3_SameHeightSwitchDropsSibling:OpEngineSequenceMatrixSuite/S4_*"`；Boost 不支持排除语法。）

- [ ] **Step 4: 逐个确认红与归因**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
bash tools/mutation/run.sh N1 N2 NEW-3; echo "rc=$? (期望 0，且无 STILL GREEN / NOT ATTRIBUTED / APPLY FAILED)"
```

- [ ] **Step 5: 提交**

```bash
git add tools/mutation/
git commit -m "test(matrix): add the fix-reversal mutation harness with three seeds"
```

---

## Task 9: PR gate 的工件覆盖（`[corpus]` + `[fisco]` 两处都要改）

**Files:**
- Modify `[corpus]`: `.github/actions/opstack-t8n-regen/action.yml`、`opstack-executor/tests/t8n/generator/ensure-vectors.sh`
- Modify `[fisco]`: `.github/workflows/workflow.yml`（两处 ref/SHA）

**现状（已核实，评审 F3）**：FISCO PR gate 检出的语料资产与执行的 action **固定在同一 SHA** `759a9af0…`——`workflow.yml:144` 是 checkout 的 `ref:`，`:157` 与 `:401` 是 `uses: FISCO-BCOS/op-stack-e2e-tests/.github/actions/opstack-t8n-regen@759a9af0…`。而且该 action 实际调用 `ensure-vectors.sh`（不是 `regen.sh`），后者**只 provision op-geth**（`ensure-vectors.sh:33-67`），runner 上没有 optimism 树。所以：

1. 语料仓 `main` 上的新 `regen.sh` / matrix 契约文件**不会**进入 PR gate，直到两处 ref 一起 bump；
2. 新加的 opnodewin 构建需要 `OP_NODE_REPO` 指向 pin 的 optimism 树，否则 regen 在 `git rev-parse` 断言处失败；
3. 工件缺失时矩阵测试 skip → PR 不会红，但 M1 永远不跑，P1 验收「`matrix cells checked > 0`」无法达成。

**「FISCO 侧无需改动」是错的**，按下面三步做。

- [ ] **Step 1: 扩展 `ensure-vectors.sh`（把 CL 参照树 provision 进去）**

在它 clone op-geth 的那段附近加入：
```bash
# provision the CL reference tree that the op-node window dumper builds against
OP_NODE_PIN="${OP_NODE_PIN:-76e4fad54244ec6bd07dad07e42c82a16ab5113a}"
OP_NODE_REPO="${OP_NODE_REPO:-$WORK/optimism}"
if [ ! -d "$OP_NODE_REPO/.git" ]; then
  git clone --filter=blob:none https://github.com/ethereum-optimism/optimism.git "$OP_NODE_REPO"
fi
git -C "$OP_NODE_REPO" fetch --depth 1 origin "$OP_NODE_PIN"
git -C "$OP_NODE_REPO" checkout --detach "$OP_NODE_PIN"
export OP_NODE_REPO OP_NODE_PIN
```
并在 `action.yml` 里确认这两个变量被导出到调用 `ensure-vectors.sh`/`regen.sh` 的步骤环境（`go-version` 保持 `'1.24.x'`）。

- [ ] **Step 2: corpus PR 合并后 bump FISCO 的两处 ref**

把 `workflow.yml:144` 的 checkout `ref:` 与 `:157`/`:401` 的 action SHA 一起改成**语料仓合并后的新提交**。本 Step 只产出「待 bump 清单 + 目标 SHA」，实际 bump 属发布动作（合入语料仓 PR 后执行），**不要**在语料仓 PR 未合并时先改 FISCO 的 ref（会指向不存在的提交）。

- [ ] **Step 3: 验证 PR gate 真的跑了 M1（而非 skip）**

合入后在 PR 日志里确认 `OpEngineApiMatrixSuite` 打印出 `matrix cells checked: N`（N>0）。若仍是 `matrix artifacts absent ... skipping`，说明：
- 资产/action 的 SHA 没一起 bump，或
- `ensure-vectors.sh` 没 provision optimism 树（Step 1 未生效）。
两种情况都要当失败处理——**skip 不是通过**。

---

## P1 验收判据（来自 spec §9 P1）

- [ ] 两个 dumper 已并入 `regen.sh`：`OPGETH=… bash regen.sh` 一次跑通（rc=0），产出 `matrix/{caps.json,engine_api_windows.json}` 且 `manifest.txt`/`SHA256SUMS` 入库、判据 ③ 不含生成物；
- [ ] M1：`OpEngineApiMatrixSuite` 绿，`matrix cells checked > 0`，且**只有 karst/getPayload 一条** recorded deviation；
- [ ] M4：`OpEngineSequenceMatrixSuite` 15 用例（S1–S14 + 门控随机）全绿，每步 I1–I6 通过；
- [ ] 变异：`run.sh N1 N2 NEW-3` rc=0；每行 `RED as required` **且归因成立**（`also_green` 覆盖的其余序列仍绿，无 `NOT ATTRIBUTED`）；
- [ ] Task 7a 的抽取是纯重构：`OpEngineImportFcuTest` 仍 30 例全绿，无重复定义；
- [ ] 回归：**spec §7 列出的全部既有 target** 全绿且用例数只增不减（7a 不改变用例数）。计数一律用 `--list_content`（比 grep `Running N test cases` 稳）：
```bash
for t in engine/test/test-bcos-engine opstack-executor/tests/opstack-executor-block-tests \
         opstack-executor/tests/opstack-executor-tests opstack-executor/tests/opstack-executor-scheduler-tests \
         opstack-executor/tests/opstack-executor-receipt-tests bcos-evm/test/bcos-evm-opstack-tests \
         bcos-tool/test/test-bcos-tool bcos-ledger/test/test-bcos-ledger bcos-rpc/test/test-bcos-rpc \
         bcos-tars-protocol/test/test-bcos-tars-protocol; do
  n=$(build/$t --list_content 2>/dev/null | grep -cE '^    [A-Za-z_][A-Za-z0-9_]*\*$')
  printf "%-46s %s\n" "${t##*/}" "$n"
done
```
基线（as-of `4e0d2c893`）：294 / 142 / 123 / 37 / 26 / 173 / 111 / 234 / 313 / 136；`test-bcos-engine` 本期应为 **294 + 新用例数**。
- [ ] 两个仓各自 PR 就绪（corpus：generator+matrix 契约；fisco：测试+harness+CI）。

## P2–P4 纲要（细则另出计划）

- **P2**：`opt8n-ref` 扩展 getPayload golden（V2–V5 × 9 forks）/receipt 字段基线/header RLP 字段集基线；M2 每响应格带回灌断言与 V4/V5 字节不变回归门；M3 溢出与非 1e6 倍 cell（op-reth 公式 oracle 标 `cannot-determine` + 最小读取集）；M5 registry 全链扫描进 nightly。
- **P3**：随机序列升到 1e5 步并进 nightly；变异补齐 41 个变体全量进 weekly；评审流程收尾清单加入「新 finding 同步追加变体」。
- **P4**：conformance 清单（握手/建块/导入/safe·finalized 的格子与判据）文档化，实现归 S7；S7 交付后逐格打勾。
