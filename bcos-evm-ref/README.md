# bcos-evm-ref

Spec: `bcos-evm/docs/superpowers/specs/2026-07-08-bcos-evm-ref-evmone-reuse-design.md` (rev.3)

复用 evmone::state（vcpkg overlay port，REF 3585c2cb）的标准 ETH/OpStack 参考模块。
与现有 bcos-evm/ 严格隔离（互不 include）。

## Build (standalone)

    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
    cmake --build build

## Build (in-tree, as part of FISCO-BCOS root build)

    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -DBCOS_EVM_REF=ON
    cmake --build build --target bcos-evm-ref-eth

`BCOS_EVM_REF`（根 `CMakeLists.txt`，默认 `OFF`）挂接本模块的 `add_subdirectory`；关闭时对现有构建零影响。

## Test

    export EVM_REF_EEST_ROOT=<EEST fixtures root>   # 未设置则 EEST 用例 SKIP
    ctest --test-dir build --output-on-failure

## M2 验收记录

- EEST release: 见 `test/EEST_VERSION`（v5.4.0, `fixtures_develop.tar.gz`；与 evmone REF 3585c2cb 配对；Task 5 Step 1 选定）
- state fixtures（harness 日志原样）：`files=2723 skipped=0 failed_files=0`
  （耗时约 17–19s；`ctest --test-dir bcos-evm-ref/build -R BcosEvmRefEthTests` 全绿）
  - 2723 个 fixture 文件覆盖全部 fork 子目录；其中 **2717 个文件含 Cancun+ case**
    （harness 内部按 `rev >= EVMC_CANCUN` 过滤，非 Cancun+ 的旧 fork 文件被遍历计数但不产生断言）
  - **Cancun+ case 总数 55,233，全部通过**（63,556 全 fork case 中）
- skip 清单（`EVM_REF_EEST_SKIP`）：无（本次运行未设置该变量，0 skip）
- 数字来源：Task 5 审查者独立复测（详见 `.superpowers/sdd/task-5-report.md` 控制器更正节）
