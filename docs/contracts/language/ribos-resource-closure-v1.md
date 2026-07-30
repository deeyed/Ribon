---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/ir/include/ribos/ir/analysis.h
  - language/ribos/ir/src/analysis.c
  - language/ribos/ir/include/ribos/ir/ir.h
  - language/ribos/frontend/src/lower.c
tests:
  - make check-ribos-resources
  - make check-ribos-semantics
  - make check-ribos-ir
  - make check
hardware:
  - none
supersedes:
  - implicit Policy IR execution-resource estimates
---

# Ribos CFG와 resource closure v1 계약

## 목적과 권한

Resource closure는 Policy IR v1.1을 bytecode로 선택하기 전에 실행 가능한 모든
control-flow와 storage를 유한한 값으로 닫는 VM 독립 분석이다. 분석기는 Pegen,
frontend AST와 source semantic table을 읽지 않는다.

분석 결과는 다음 consumer의 공통 입력이다.

```text
host compiler budget gate
deterministic bytecode emitter
independent artifact verifier
VM runtime budget initialization
diagnostic/source-map tool
```

Host compiler의 성공 결과를 그대로 신뢰해서는 안 된다. Artifact verifier는
serialized IR 또는 bytecode에서 같은 bound를 다시 계산하고, VM은 봉인된 runtime
counter를 실행 중에 감소시켜야 한다.

## CFG reachability와 terminal closure

각 function의 entry block에서 direct `JUMP`와 `BRANCH` edge를 따라 reachable block을
계산한다. Unreachable block은 실행량, helper와 terminal bound에 기여하지 않는다.
Emitter는 unreachable block을 executable artifact에서 제거할 수 있다.

모든 reachable path는 다음 중 하나로 닫혀야 한다.

- function의 `RETURN`
- fail-closed `TRAP`
- bounded loop의 검증된 back-edge

Loop table에 속하지 않는 reachable cycle, 유효하지 않은 direct target, terminal
action으로 닫히지 않는 path와 recursive direct call graph는 verification failure다.
Direct callee의 `TRAP`은 caller를 재개하지 않는 terminal action으로 전파한다.

## Bounded loop

Loop row는 다음 값을 봉인한다.

```text
function ID
header block
body entry block
exit block
latch block 또는 INVALID_ID
maximum trip count
source map
```

Header의 terminal instruction은 `body`와 `exit`로 가는 `BRANCH`이고 immediate는
maximum trip count와 같아야 한다. Latch가 있으면 그 block의 terminal instruction은
header로 직접 가는 `JUMP`다.

Loop body가 header에 재진입할 수 있으면 header는 최대 `N + 1`, body path는 최대
`N`번 실행된다. Body가 첫 iteration에서 항상 `RETURN` 또는 `TRAP`이면 latch는 없고
body는 최대 한 번 실행된다. Nested loop bound는 바깥 loop bound와 곱하되 `u64`
상한을 넘으면 resource closure를 거부한다.

## Instruction과 helper upper bound

Policy IR instruction 하나의 analysis cost는 1이다. Worst-path upper bound는 다음
규칙으로 계산한다.

- sequential instruction은 더한다.
- branch alternative는 각 metric의 큰 값을 선택한다.
- bounded loop는 loop body worst path에 trip count를 적용한다.
- `CALL_DIRECT`는 call instruction 1과 callee upper bound를 더한다.
- unreachable instruction은 0이다.
- 산술 상한 계산이 `u64`를 넘으면 artifact를 거부한다.

Helper는 두 종류의 상한을 가진다.

- function 전체 helper call upper bound
- function과 helper stable ID별 call upper bound

Helper별 상한은 branch, nested loop와 reachable direct callee를 포함한다. 서로 다른
helper의 최대값이 서로 다른 branch에서 발생할 수 있으므로 helper별 상한의 합을
function 전체 상한으로 대신하지 않는다.

Compiler는 finite `instruction_budget`과 `helper_budget`을 분석값과 대조한다. 하나라도
초과하면 authoritative module을 반환하지 않는다.

## Type storage ABI

Layout은 host pointer size, native C alignment와 allocator에 의존하지 않는다. 모든
alignment는 최대 8 byte인 power of two다.

| Type | Representation |
| --- | --- |
| `Unit` | 0 byte |
| `bool` | 1 byte |
| `u8`/`i8` ... `u64`/`i64` | width와 같은 size/alignment |
| string literal | 8-byte immutable constant reference, align 4 |
| product enum | 4 byte, align 4 |
| product fact/value/opaque handle | 8 byte, align 8 |
| `Array[T,N]` | aligned inline `T[N]` |
| `List[T,N]` | `u32 length`와 aligned inline `T[N]` |
| map | `u32 cardinality`와 aligned sorted entry array |
| `Option`/`Result`/enum | `u8 tag`와 aligned maximum payload |
| struct | declaration-order aligned fields |

Array element와 map entry는 각 element alignment로 올림한 고정 stride를 사용한다.
Recursive by-value aggregate, 1 MiB를 넘는 단일 value와 layout 산술 overflow는
거부한다.

`FrozenMap`과 `Dict`는 모두 stable total order로 정렬된 inline entry array다.
`FrozenMap`의 cardinality는 type bound와 같고, `Dict`의 runtime cardinality는
`0..capacity`다. Lookup은 capacity 이하의 bounded linear search다. Hash seed,
resize, rehash와 heap fallback은 없다.

## Slot, frame와 call stack

Policy IR v1.1은 virtual slot ID 순서대로 frame offset을 배치하며 slot storage를
재사용하지 않는다. 각 offset은 type alignment를 만족한다.

```text
frame bytes
  = align(sum(aligned slot storage), maximum slot alignment)

maximum call depth
  = 1 + maximum reachable direct-callee depth

maximum stack bytes
  = frame bytes + maximum reachable callee stack bytes
```

이 layout은 최적 register allocation이 아니라 verifier가 쉽게 재계산할 수 있는
baseline이다. Emitter가 slot reuse를 도입하려면 별도 liveness proof와 artifact
계약이 필요하며 이 baseline보다 큰 storage를 숨길 수 없다.

Host implementation limit은 다음과 같다.

```text
single value   1 MiB
function frame 16 MiB
call stack     64 MiB
```

## Runtime 집행

Executable artifact는 최소한 다음 값을 봉인해야 한다.

```text
schema identity
instruction upper bound와 initial runtime budget
helper 전체/개별 upper bound
frame bytes와 maximum stack bytes
maximum call depth
loop trip table
terminal mask
```

VM은 instruction dispatch 전에 remaining instruction counter를 확인하고 감소시킨다.
Helper dispatch도 전체 counter와 stable-ID별 counter를 감소시킨다. Counter underflow,
frame/stack 초과와 검증된 loop count 초과는 catchable exception이 아니라 policy
fault다.

## 증거 경계

`make check-ribos-resources`는 host Policy IR에서 다음을 검증한다.

- CFG reachability와 bounded-cycle rejection
- terminal closure
- type/slot/frame/stack layout
- nested loop와 direct-call upper bound
- helper stable-ID별 upper bound
- compiler-side instruction budget rejection
- sorted-array map storage
- deterministic resource dump

이 gate는 bytecode artifact, independent hostile-byte verifier, VM counter dispatch,
Ribon boot product linkage, QEMU 또는 physical hardware 실행을 증명하지 않는다.
