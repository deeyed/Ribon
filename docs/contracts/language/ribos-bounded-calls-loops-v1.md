---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/include/ribos/vm/interpreter.h
  - language/ribos/vm/src/runtime/interpreter.c
  - language/ribos/vm/src/runtime/storage_internal.h
  - language/ribos/vm/src/runtime/storage.c
  - language/ribos/vm/tests/calls_loops_interpreter.rbs
  - language/ribos/vm/tests/calls_loops_interpreter_tests.c
  - Makefile
tests:
  - make check-ribos-vm-calls
  - make check-ribos-vm-loops
  - make check-ribos-vm-scalar
  - make check-ribos-resources
  - make check-ribos-verifier
  - make check-ribos-host-boundary
  - make check
  - make docs
hardware:
  - none
supersedes:
  - implicit direct-call frame and loop-counter execution semantics
---

# Ribos bounded direct call과 loop execution v1 계약

## 목적과 증거 경계

이 계약은 independently verified `CALL_DIRECT`와 loop row를 caller-owned arena에서
실행하는 의미를 고정한다. Native C stack, 재귀, computed target와 runtime이 새로
추론한 loop는 허용하지 않는다.

```text
verified call DAG                 verified loop row
       |                                 |
       v                                 v
explicit arena frame stack       latch/backedge counter
       |                                 |
       +---------------+-----------------+
                       v
             shared instruction fuel
```

이 계약이 제공하는 증거는 host interpreter unit과 sanitizer 실행이다. Helper
callback, sealed `BootAction`, recovery notification, firmware, QEMU와 실제 hardware
policy execution은 증명하지 않는다. Aggregate와 opaque handle의 ownership 전달은
{doc}`ribos-generation-handles-v1`이 소유한다.

## Explicit frame stack

Frame stack은 runtime storage의 frame-record region과 frame-value region만 사용한다.
C 함수 호출은 interpreter 구현의 고정 호출 깊이를 넘어서 policy call depth와
연동되지 않는다.

각 32-byte frame record는 little-endian field를 가진다.

| Offset | Field | 의미 |
| ---: | --- | --- |
| 0 | function ID | active frame의 verified function |
| 4 | continuation instruction ID | caller에서 재개할 verified instruction |
| 8 | return slot ID | caller가 결과를 받을 verified slot |
| 12 | reserved | zero |
| 16 | frame base | frame-value region 내부 byte offset |
| 24 | frame byte size | verified function frame 크기 |
| 28 | reserved | zero |

Entry record의 continuation과 return slot은 `INVALID_ID`다. 그 밖의 record에는 둘 다
유효해야 한다. Active record의 frame base는 0부터 연속이고 마지막 record의 끝은
control region의 stack cursor와 정확히 같다. Current function과 frame base는 마지막
record와 일치해야 한다.

Active function ID는 모두 달라야 한다. 따라서 verifier가 금지한 recursion이 runtime
stack에 나타나면 실행하지 않는다.

## Direct call 순서

유효한 `CALL_DIRECT` 한 개는 다음 순서를 가진다.

1. Caller의 instruction fuel 1개를 먼저 소비한다.
2. Callee function, return type, parameter 수와 exact slot layout을 다시 검사한다.
3. `frame_depth + 1`과 `stack_cursor + callee.frame_bytes`가 verifier closure 안인지
   검사한다.
4. Active frame에 같은 callee function이 없는지 검사한다.
5. Callee frame과 그 function의 slot-state range를 reset한다.
6. Caller operand를 source order로 callee parameter slot에 복사한다.
7. Callee 소유 loop counter를 verified trip count로 reset한다.
8. Continuation, caller result slot과 callee frame record를 기록한다.
9. PC를 callee entry block의 첫 instruction으로 바꾼다.

v1.3 direct call의 parameter와 result는 type ID, byte size와 function ownership이
정확히 같은 slot이다. Scalar는 exact width로, copy-only aggregate는
{doc}`ribos-bounded-aggregate-runtime-v1`의 전체 inline representation으로 복사한다.
각 function frame 크기는 8-byte 배수이며 stack closure도 frame-end padding을
포함한다. Nested function의 `PARAMETER` instruction은 이미 복사된 parameter slot이
initialized인지 재확인하는 no-op다. Entry function의 `PARAMETER`만 immutable
`RibosVmContext`에서 값을 읽는다.

Affine/linear operand는 callee parameter로 exact-copy한 뒤 caller source slot 전체를
`MOVED`로 바꾼다. Nested return도 caller result copy 뒤 callee source를 이동시킨다.
Entry return은 caller가 outcome을 가져갈 수 있도록 return slot을 initialized로
봉인한다. Dynamic generation/borrow/consume은
{doc}`ribos-generation-handles-v1`을 따른다.

## Return과 continuation

Nested `RETURN`은 initialized operand를 active frame에서 읽고 caller의 verified
result slot에 exact bytes로 복사한다. Aggregate padding과 unused capacity도 값의
일부로 전달된다. 그 뒤에만 callee frame value와 record를 지우고 caller
continuation으로 복귀한다.

Entry frame의 `RETURN`은 기존 incremental `RETURNED` state를 만든다. 이는 내부 정책
함수의 반환이며 `BootAction` 또는 full-policy success가 아니다.

Call instruction, callee instruction, nested return과 caller continuation은 각각
dispatch될 때 instruction fuel 1개를 소비한다. Callee 실행은 별도 budget을 만들지
않는다. 따라서 실제 consumed count는 같은 경로에 대해 verifier가 계산한
instruction upper bound를 넘을 수 없다.

## Call fail-closed 조건

Frame push 전에 다음을 검사한다.

- active control state와 top frame의 일치
- direct callee와 entry block/instruction의 존재
- result type과 callee return type의 일치
- parameter count, type와 byte size의 일치
- 모든 frame base와 frame byte size의 8-byte 정렬
- continuation instruction이 caller function에 속하는지
- return slot이 caller function에 속하는지
- verifier call depth 이내인지
- verifier maximum stack bytes 이내인지
- active function과 중복되지 않는지

Depth 또는 stack 위반은 각각 `CALL_DEPTH`, `STACK_BOUNDS` fault다. Malformed
continuation, slot, parameter 또는 frame record는 `INVALID_VALUE` 또는 `INTERNAL`로
봉인한다. Runtime은 malformed call을 native return address나 function pointer로
해석하지 않는다.

## Loop counter 의미

Runtime은 artifact loop row의 다음 identity만 집행한다.

- owner function
- header block
- body block
- exit block
- latch block
- statically verified trip count

Counter는 unsigned 64-bit little-endian 값이며 function entry와 external header
entry에서 trip count로 reset한다.

```text
external block -> header : counter = trip_count
header -> body           : counter > 0인지 검사
latch -> header          : counter > 0인지 검사한 뒤 counter -= 1
header -> exit           : counter 변경 없음
```

Latch가 아닌 block에서 header로 들어오는 transition은 새 loop activation이다. 이
규칙은 inner loop가 outer loop의 다음 iteration에서 다시 전체 상한을 갖게 한다.
Verified latch/backedge만 counter를 감소시킬 수 있다.

Trip count가 `N`이면 body 진입은 최대 `N`회다. `N`번째 latch 뒤 counter는 0이고
header에서 exit로 갈 수 있다. 다시 body로 가려 하면 그 branch가 이미 소비한
instruction count를 유지한 채 `LOOP_BOUND` receipt를 봉인한다. Receipt detail은
위반한 stable loop ID다.

## Resource closure와 runtime 비교

Runtime은 compiler metadata를 새 권위로 사용하지 않는다. PreparedProgram의 independent
verifier report가 재계산한 다음 값과 arena state를 대조한다.

- maximum call depth
- maximum direct-call stack bytes
- entry instruction upper bound
- function별 frame bytes
- loop별 trip count

Focused call gate는 실제 최대 frame depth와 stack cursor가 verifier report와 같고,
callee dispatch 수가 encoded callee upper bound와 같은지 검사한다. Focused loop
gate는 `N`회 body와 latch dispatch, `N + 1`번째 body 거부와 external re-entry reset을
검사한다.

## Gate

```sh
make check-ribos-vm-calls
make check-ribos-vm-loops
```

두 gate는 같은 tracked `.rbs` fixture를 독립 artifact로 만들고 authorization,
Stage-1/Stage-2 verifier와 scalar interpreter gate를 먼저 통과시킨다.

Call gate는 다음을 검사한다.

- 두 단계 nested direct call
- scalar와 copy-only aggregate argument/result의 exact transfer
- explicit push/pop과 caller continuation
- sequential callee frame reuse
- runtime depth/stack과 verifier closure 일치
- callee actual dispatch와 verifier upper bound 일치
- depth overflow의 push 전 non-mutating rejection
- nested instruction-budget fault의 function/instruction provenance

Loop gate는 다음을 검사한다.

- 두 verified loop row
- header-to-body 검사와 latch-to-header 감소
- exact dispatch accounting
- trip count 소진 뒤 `LOOP_BOUND`
- loop ID, function과 instruction receipt
- external header entry의 counter reset
