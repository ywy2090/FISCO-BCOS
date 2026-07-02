<<<<<<< Updated upstream
#!/ bin / bash
#Measure valid_insertions per eth PR slice using tools /.ci / check - commit.sh formula.
set - euo pipefail

          BASE = "${1:-fisco/release-3.18.0}" HEAD = "${2:-HEAD}" INSERT_LIMIT = 300 LICENSE_LINE =
    20

    check_slice()
{
    local label =
        "$1" shift local - a paths = ("$@")

            local need_check_files need_check_files =
                $(git diff-- numstat
                        "$BASE"
                        "$HEAD" --"${paths[@]}" |
                        awk '{if ($1!=0) print $0;}' | sed 's/{.*> //g;s/}//g' | awk '{print $3}' |
                        grep - vE 'sample/|benchmark/|test|tools/|fisco-bcos/|\.github/' |
                        grep - E '\.(h|hpp|c|cpp)$' ||
                    true)

                    if[-z "$need_check_files"];
    then printf
        "%-42s valid=%4d raw=%5d git=%5d files=%2d new=%2d  %s\n"
        "$label" 0 0 0 0 0 "OK" return fi

#Build file array for git diff--
            local -
        a files = () while IFS = read - r f;
    do
        [-n "$f"]&& files +=
            ("$f")done < < < "$need_check_files"

            local new_files empty_lines block_lines include_lines comment_lines insertions git_ins
                valid result new_files = $(git diff
                                               "$BASE"
                                               "$HEAD" --"${files[@]}" |
                                               grep - c 'new file mode' ||
                                           true) empty_lines = $(git diff
                                                                     "$BASE"
                                                                     "$HEAD" --"${files[@]}" |
                                                                     grep - cE '^\+\s*$' ||
                                                                 true) block_lines =
                $(git diff
                        "$BASE"
                        "$HEAD" --"${files[@]}" |
                        grep - cE '^\+\s*[\{\}]\s*$' ||
                    true) include_lines = $(git diff
                                                "$BASE"
                                                "$HEAD" --"${files[@]}" |
                                                grep - cE '^\+\#include' ||
                                            true) comment_lines = $(git diff
                                                                        "$BASE"
                                                                        "$HEAD" --"${files[@]}" |
                                                                        grep - cE '^\+\s*//' ||
                                                                    true) insertions =
                    $(git diff-- ignore - space -
                            change-- shortstat
                            "$BASE"
                            "$HEAD" --"${files[@]}" |
                        awk '{print $4}') insertions =
        ${insertions : -0} git_ins = $(git diff-- shortstat
                                       "$BASE"
                                       "$HEAD" --"${files[@]}" |
                                       awk '{print $4}') git_ins = ${git_ins : -0} valid =
            $((insertions - new_files * LICENSE_LINE - comment_lines - empty_lines - block_lines -
                include_lines)) result =
                "OK" if["$valid" - gt "$INSERT_LIMIT"] && ["$git_ins" - gt "$INSERT_LIMIT"];
    then result = "FAIL" fi printf
                  "%-42s valid=%4d raw=%5d git=%5d files=%2d new=%2d  %s\n"
                  "$label"
                  "$valid"
                  "$insertions"
                  "$git_ins"
                  "${#files[@]}"
                  "$new_files"
                  "$result"
}

echo "=== check-commit.sh valid_insertions per eth PR slice ===" echo "BASE=$BASE  HEAD=$HEAD" echo
     "Formula: valid = insertions - new_files*${LICENSE_LINE} - //comments - empty+ - {} - "
     "#include" echo "FAIL when valid>${INSERT_LIMIT} AND git>${INSERT_LIMIT}" echo ""

    check_slice "PR-01 prod-1" bcos -
    evm / CMakeLists.txt bcos - evm / eth / vm / bcos - evm / eth / RevisionConfig.h bcos -
    evm / eth / Web3TypedTxKind.h bcos - evm / eth / policy / bcos -
    evm / eth / gas /
        ProtocolGas.h

            check_slice "PR-01 prod-2" bcos -
    evm / eth / state / Account.hpp bcos - evm / eth / state / BlockInfo.hpp bcos -
    evm / eth / state / Transaction.hpp bcos - evm / eth / state / StateDiff.hpp bcos -
    evm / eth / state /
        StateView.hpp

            check_slice "PR-01 ALL" bcos -
    evm / CMakeLists.txt bcos - evm / eth / vm / bcos - evm / eth / RevisionConfig.h bcos -
    evm / eth / Web3TypedTxKind.h bcos - evm / eth / policy / bcos -
    evm / eth / gas / ProtocolGas.h bcos - evm / eth / state / Account.hpp bcos -
    evm / eth / state / BlockInfo.hpp bcos - evm / eth / state / Transaction.hpp bcos -
    evm / eth / state / StateDiff.hpp bcos -
    evm / eth / state /
        StateView.hpp

            check_slice "PR-02 prod-1" bcos -
    evm / eth / state / HashUtils.hpp bcos -
    evm / eth / state / State.hpp check_slice "PR-02 prod-2" bcos -
    evm / eth / state / State.cpp check_slice "PR-02 ALL" bcos -
    evm / eth / state / HashUtils.hpp bcos - evm / eth / state / State.hpp bcos -
    evm / eth / state /
        State.cpp

            check_slice "PR-03 prod-1" bcos -
    evm / eth / gas / TxIntrinsicGas.h bcos -
    evm / eth / gas / TxFeeSettlement.h check_slice "PR-03 prod-2" bcos -
    evm / eth / eip / Eip1559.h bcos - evm / eth / eip / Eip1559Gate.h bcos -
    evm / eth / eip / Eip2929Gate.h bcos - evm / eth / eip / Eip2929StorageGas.h bcos -
    evm / eth / eip / Eip2930AccessList.h bcos - evm / eth / eip / Eip4844.h bcos -
    evm / eth / eip / Eip7623.h bcos - evm / eth / eip / Eip7702.h check_slice "PR-03 ALL" bcos -
    evm / eth / gas / TxIntrinsicGas.h bcos - evm / eth / gas / TxFeeSettlement.h bcos -
    evm / eth / eip /

        check_slice "PR-04 ALL" bcos -
    evm / eth / eip / Eip7702.cpp bcos -
    evm / eth / trace /
        EvmTrace.h

            check_slice "PR-05 prod-1" bcos -
    evm / eth / core / check_slice "PR-05 prod-2" bcos - evm / eth / kernel / CallKind.h bcos -
    evm / eth / kernel / FrameScope.h bcos - evm / eth / kernel / InnerExecuteTypes.h bcos -
    evm / eth / kernel / EVMCResult.h bcos -
    evm / eth / kernel / EVMCResult.cpp check_slice "PR-05 ALL" bcos - evm / eth / core / bcos -
    evm / eth / kernel / CallKind.h bcos - evm / eth / kernel / FrameScope.h bcos -
    evm / eth / kernel / InnerExecuteTypes.h bcos - evm / eth / kernel / EVMCResult.h bcos -
    evm / eth / kernel /
        EVMCResult.cpp

            check_slice "PR-06 ALL" bcos -
    evm / eth / precompiled / PrecompileRouter.h bcos -
    evm / eth / precompiled / PrecompileRouter.cpp bcos -
    evm / eth / precompiled / PrecompileActive.h bcos -
    evm / eth / precompiled / EthPrecompiles.h bcos -
    evm / eth / precompiled /
        Eip2537Gas.h

            check_slice "PR-07 ALL" bcos -
    evm / eth / precompiled / PrecompiledAddress.h bcos -
    evm / eth / precompiled / ModexpGas.h bcos -
    evm / eth / precompiled /
        ModexpGas.cpp

            check_slice "PR-08 ALL" bcos -
    evm / eth / precompiled / EthPrecompiles.cpp check_slice "PR-09 Registry" bcos -
    executor / src / vm / EthBuiltinRegistry.cpp bcos -
    executor / src / vm / PrecompiledContract.cpp check_slice "PR-10 ALL" bcos -
    evm / eth / host / bcos -
    evm / eth / apply /
        EthEvmHostHooks.h

            check_slice "PR-11 prod-1" bcos -
    evm / eth / kernel / execution / CreateAddress.h bcos -
    evm / eth / kernel / execution / CreateAddress.cpp bcos -
    evm / eth / kernel / execution / FrameRouting.h bcos -
    evm / eth / kernel / execution / FrameRouting.cpp bcos -
    evm / eth / kernel / execution / FrameBytecode.h bcos -
    evm / eth / kernel / execution / FrameBytecode.cpp bcos -
    evm / eth / kernel / execution / CallTargetResolver.h bcos -
    evm / eth / kernel / execution / CallTargetResolver.cpp check_slice "PR-11 prod-2" bcos -
    evm / eth / kernel / execution / PrepareState.h bcos -
    evm / eth / kernel / execution / CreateDeployment.h check_slice "PR-11 ALL" bcos -
    evm / eth / kernel / execution / CreateAddress.h bcos -
    evm / eth / kernel / execution / CreateAddress.cpp bcos -
    evm / eth / kernel / execution / FrameRouting.h bcos -
    evm / eth / kernel / execution / FrameRouting.cpp bcos -
    evm / eth / kernel / execution / FrameBytecode.h bcos -
    evm / eth / kernel / execution / FrameBytecode.cpp bcos -
    evm / eth / kernel / execution /
        CreateDeployment.h

            check_slice "PR-12 prod-1" bcos -
    evm / eth / kernel / execution / EvmCallFrame.h bcos -
    evm / eth / kernel / execution / EvmCallFrame.cpp check_slice "PR-12 prod-2" bcos -
    evm / eth / kernel / execution / InnerExecute.h bcos -
    evm / eth / kernel / execution / InnerExecute.cpp check_slice "PR-12 ALL" bcos -
    evm / eth / kernel / execution / EvmCallFrame.h bcos -
    evm / eth / kernel / execution / EvmCallFrame.cpp bcos -
    evm / eth / kernel / execution / InnerExecute.h bcos -
    evm / eth / kernel / execution /
        InnerExecute.cpp

            check_slice "PR-13 prod-1" bcos -
    evm / eth / kernel / state - transition / StateTransitionContext.h bcos -
    evm / eth / kernel / state - transition / StateTransitionContext.cpp bcos -
    evm / eth / kernel / state - transition / DeductIntrinsicGas.h bcos -
    evm / eth / kernel / state - transition / IntrinsicGasAccounting.h bcos -
    evm / eth / kernel / state - transition / FeeInputsMapping.h check_slice "PR-13 prod-2" bcos -
    evm / eth / kernel / state - transition / StateTransitionErrorPolicy.h bcos -
    evm / eth / kernel / state - transition / IncludedTxVmerrNormalize.h bcos -
    evm / eth / kernel / state - transition / StateTransitionExecute.h bcos -
    evm / eth / kernel / state -
    transition / StateTransitionExecute.cpp check_slice "PR-13 ALL" bcos -
    evm / eth / kernel / state -
    transition /

        check_slice "PR-14 ALL" bcos -
    evm / eth / apply / EthStateTransitionBindings.h bcos -
    evm / eth / apply / EthStateTransitionBindings.cpp bcos -
    evm / eth / apply / EthStateTransitionHooks.h bcos -
    evm / eth / apply / EthStateTransitionHooks.cpp bcos -
    evm / eth / apply / EthStateTransitionErrorPolicy.h bcos -
    evm / eth / apply /
        EthEvmResult.h

            check_slice "PR-15 ALL" bcos -
    evm / eth / apply / ApplyEthMessage.h bcos - evm / eth / apply / ApplyEthMessage.cpp bcos -
    evm / eth / apply /
        EthTxFeeSettlement.h

            echo "" check_slice "eth/ ALL" bcos -
    evm / eth /
=======
#!/bin/bash
# Measure valid_insertions per eth PR slice using tools/.ci/check-commit.sh formula.
set -euo pipefail

BASE="${1:-fisco/release-3.18.0}"
HEAD="${2:-HEAD}"
INSERT_LIMIT=300
LICENSE_LINE=20

check_slice() {
  local label="$1"
  shift
  local -a paths=("$@")

  local need_check_files
  need_check_files=$(git diff --numstat "$BASE" "$HEAD" -- "${paths[@]}" \
    | awk '{if ($1!=0) print $0;}' \
    | sed 's/{.*> //g;s/}//g' \
    | awk '{print $3}' \
    | grep -vE 'sample/|benchmark/|test|tools/|fisco-bcos/|\.github/' \
    | grep -E '\.(h|hpp|c|cpp)$' || true)

  if [ -z "$need_check_files" ]; then
    printf "%-42s valid=%4d raw=%5d git=%5d files=%2d new=%2d  %s\n" "$label" 0 0 0 0 0 "OK"
    return
  fi

  # Build file array for git diff --
  local -a files=()
  while IFS= read -r f; do
    [ -n "$f" ] && files+=("$f")
  done <<< "$need_check_files"

  local new_files empty_lines block_lines include_lines comment_lines insertions git_ins valid result
  new_files=$(git diff "$BASE" "$HEAD" -- "${files[@]}" | grep -c 'new file mode' || true)
  empty_lines=$(git diff "$BASE" "$HEAD" -- "${files[@]}" | grep -cE '^\+\s*$' || true)
  block_lines=$(git diff "$BASE" "$HEAD" -- "${files[@]}" | grep -cE '^\+\s*[\{\}]\s*$' || true)
  include_lines=$(git diff "$BASE" "$HEAD" -- "${files[@]}" | grep -cE '^\+\#include' || true)
  comment_lines=$(git diff "$BASE" "$HEAD" -- "${files[@]}" | grep -cE '^\+\s*//' || true)
  insertions=$(git diff --ignore-space-change --shortstat "$BASE" "$HEAD" -- "${files[@]}" | awk '{print $4}')
  insertions=${insertions:-0}
  git_ins=$(git diff --shortstat "$BASE" "$HEAD" -- "${files[@]}" | awk '{print $4}')
  git_ins=${git_ins:-0}
  valid=$((insertions - new_files * LICENSE_LINE - comment_lines - empty_lines - block_lines - include_lines))
  result="OK"
  if [ "$valid" -gt "$INSERT_LIMIT" ] && [ "$git_ins" -gt "$INSERT_LIMIT" ]; then
    result="FAIL"
  fi
  printf "%-42s valid=%4d raw=%5d git=%5d files=%2d new=%2d  %s\n" \
    "$label" "$valid" "$insertions" "$git_ins" "${#files[@]}" "$new_files" "$result"
}

echo "=== check-commit.sh valid_insertions per eth PR slice ==="
echo "BASE=$BASE  HEAD=$HEAD"
echo "Formula: valid = insertions - new_files*${LICENSE_LINE} - //comments - empty+ - {} - #include"
echo "FAIL when valid>${INSERT_LIMIT} AND git>${INSERT_LIMIT}"
echo ""

check_slice "PR-01 prod-1" \
  bcos-evm/CMakeLists.txt bcos-evm/eth/vm/ bcos-evm/eth/RevisionConfig.h \
  bcos-evm/eth/Web3TypedTxKind.h bcos-evm/eth/policy/ bcos-evm/eth/gas/ProtocolGas.h

check_slice "PR-01 prod-2" \
  bcos-evm/eth/state/Account.hpp bcos-evm/eth/state/BlockInfo.hpp \
  bcos-evm/eth/state/Transaction.hpp bcos-evm/eth/state/StateDiff.hpp bcos-evm/eth/state/StateView.hpp

check_slice "PR-01 ALL" \
  bcos-evm/CMakeLists.txt bcos-evm/eth/vm/ bcos-evm/eth/RevisionConfig.h \
  bcos-evm/eth/Web3TypedTxKind.h bcos-evm/eth/policy/ bcos-evm/eth/gas/ProtocolGas.h \
  bcos-evm/eth/state/Account.hpp bcos-evm/eth/state/BlockInfo.hpp \
  bcos-evm/eth/state/Transaction.hpp bcos-evm/eth/state/StateDiff.hpp bcos-evm/eth/state/StateView.hpp

check_slice "PR-02 prod-1" bcos-evm/eth/state/HashUtils.hpp bcos-evm/eth/state/State.hpp
check_slice "PR-02 prod-2" bcos-evm/eth/state/State.cpp
check_slice "PR-02 ALL" bcos-evm/eth/state/HashUtils.hpp bcos-evm/eth/state/State.hpp bcos-evm/eth/state/State.cpp

check_slice "PR-03 prod-1" bcos-evm/eth/gas/TxIntrinsicGas.h bcos-evm/eth/gas/TxFeeSettlement.h
check_slice "PR-03 prod-2" bcos-evm/eth/eip/Eip1559.h bcos-evm/eth/eip/Eip1559Gate.h \
  bcos-evm/eth/eip/Eip2929Gate.h bcos-evm/eth/eip/Eip2929StorageGas.h \
  bcos-evm/eth/eip/Eip2930AccessList.h bcos-evm/eth/eip/Eip4844.h bcos-evm/eth/eip/Eip7623.h bcos-evm/eth/eip/Eip7702.h
check_slice "PR-03 ALL" bcos-evm/eth/gas/TxIntrinsicGas.h bcos-evm/eth/gas/TxFeeSettlement.h bcos-evm/eth/eip/

check_slice "PR-04 ALL" bcos-evm/eth/eip/Eip7702.cpp bcos-evm/eth/trace/EvmTrace.h

check_slice "PR-05 prod-1" bcos-evm/eth/core/
check_slice "PR-05 prod-2" bcos-evm/eth/kernel/CallKind.h bcos-evm/eth/kernel/FrameScope.h \
  bcos-evm/eth/kernel/InnerExecuteTypes.h bcos-evm/eth/kernel/EVMCResult.h bcos-evm/eth/kernel/EVMCResult.cpp
check_slice "PR-05 ALL" bcos-evm/eth/core/ bcos-evm/eth/kernel/CallKind.h bcos-evm/eth/kernel/FrameScope.h \
  bcos-evm/eth/kernel/InnerExecuteTypes.h bcos-evm/eth/kernel/EVMCResult.h bcos-evm/eth/kernel/EVMCResult.cpp

check_slice "PR-06 ALL" \
  bcos-evm/eth/precompiled/PrecompileRouter.h bcos-evm/eth/precompiled/PrecompileRouter.cpp \
  bcos-evm/eth/precompiled/PrecompileActive.h \
  bcos-evm/eth/precompiled/EthPrecompiles.h \
  bcos-evm/eth/precompiled/Eip2537Gas.h

check_slice "PR-07 ALL" \
  bcos-evm/eth/precompiled/PrecompiledAddress.h bcos-evm/eth/precompiled/ModexpGas.h bcos-evm/eth/precompiled/ModexpGas.cpp

check_slice "PR-08 ALL" bcos-evm/eth/precompiled/EthPrecompiles.cpp
check_slice "PR-09 Registry" bcos-executor/src/vm/EthBuiltinRegistry.cpp \
  bcos-executor/src/vm/PrecompiledContract.cpp
check_slice "PR-10 ALL" bcos-evm/eth/host/ bcos-evm/eth/apply/EthEvmHostHooks.h

check_slice "PR-11 prod-1" \
  bcos-evm/eth/kernel/execution/CreateAddress.h bcos-evm/eth/kernel/execution/CreateAddress.cpp \
  bcos-evm/eth/kernel/execution/FrameRouting.h bcos-evm/eth/kernel/execution/FrameRouting.cpp \
  bcos-evm/eth/kernel/execution/FrameBytecode.h bcos-evm/eth/kernel/execution/FrameBytecode.cpp \
  bcos-evm/eth/kernel/execution/CallTargetResolver.h bcos-evm/eth/kernel/execution/CallTargetResolver.cpp
check_slice "PR-11 prod-2" \
  bcos-evm/eth/kernel/execution/PrepareState.h \
  bcos-evm/eth/kernel/execution/CreateDeployment.h
check_slice "PR-11 ALL" \
  bcos-evm/eth/kernel/execution/CreateAddress.h bcos-evm/eth/kernel/execution/CreateAddress.cpp \
  bcos-evm/eth/kernel/execution/FrameRouting.h bcos-evm/eth/kernel/execution/FrameRouting.cpp \
  bcos-evm/eth/kernel/execution/FrameBytecode.h bcos-evm/eth/kernel/execution/FrameBytecode.cpp \
  bcos-evm/eth/kernel/execution/CallTargetResolver.h bcos-evm/eth/kernel/execution/CallTargetResolver.cpp \
  bcos-evm/eth/kernel/execution/PrepareState.h \
  bcos-evm/eth/kernel/execution/CreateDeployment.h

check_slice "PR-12 prod-1" bcos-evm/eth/kernel/execution/EvmCallFrame.h bcos-evm/eth/kernel/execution/EvmCallFrame.cpp
check_slice "PR-12 prod-2" \
  bcos-evm/eth/kernel/execution/InnerExecute.h bcos-evm/eth/kernel/execution/InnerExecute.cpp
check_slice "PR-12 ALL" \
  bcos-evm/eth/kernel/execution/EvmCallFrame.h bcos-evm/eth/kernel/execution/EvmCallFrame.cpp \
  bcos-evm/eth/kernel/execution/InnerExecute.h bcos-evm/eth/kernel/execution/InnerExecute.cpp

check_slice "PR-13 prod-1" \
  bcos-evm/eth/kernel/state-transition/StateTransitionContext.h bcos-evm/eth/kernel/state-transition/StateTransitionContext.cpp \
  bcos-evm/eth/kernel/state-transition/DeductIntrinsicGas.h bcos-evm/eth/kernel/state-transition/IntrinsicGasAccounting.h \
  bcos-evm/eth/kernel/state-transition/FeeInputsMapping.h
check_slice "PR-13 prod-2" \
  bcos-evm/eth/kernel/state-transition/StateTransitionErrorPolicy.h \
  bcos-evm/eth/kernel/state-transition/IncludedTxVmerrNormalize.h \
  bcos-evm/eth/kernel/state-transition/StateTransitionExecute.h bcos-evm/eth/kernel/state-transition/StateTransitionExecute.cpp
check_slice "PR-13 ALL" bcos-evm/eth/kernel/state-transition/

check_slice "PR-14 ALL" \
  bcos-evm/eth/apply/EthStateTransitionBindings.h bcos-evm/eth/apply/EthStateTransitionBindings.cpp \
  bcos-evm/eth/apply/EthStateTransitionHooks.h bcos-evm/eth/apply/EthStateTransitionHooks.cpp \
  bcos-evm/eth/apply/EthStateTransitionErrorPolicy.h \
  bcos-evm/eth/apply/EthEvmResult.h

check_slice "PR-15 ALL" \
  bcos-evm/eth/apply/ApplyEthMessage.h bcos-evm/eth/apply/ApplyEthMessage.cpp bcos-evm/eth/apply/EthTxFeeSettlement.h

echo ""
check_slice "eth/ ALL" bcos-evm/eth/
>>>>>>> Stashed changes
