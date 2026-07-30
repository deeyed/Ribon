---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/include/ribos/vm/interpreter.h
  - language/ribos/vm/src/runtime/interpreter.c
  - language/ribos/vm/src/runtime/storage_internal.h
  - language/ribos/vm/src/runtime/storage.c
  - docs/contracts/language/ribos-bounded-calls-loops-v1.md
tests:
  - make check-ribos-vm-calls
  - make check-ribos-vm-loops
  - make check-ribos-vm-scalar
  - make check-ribos-resources
  - make check-ribos-verifier
  - make check
  - make docs
hardware:
  - none
supersedes:
  - implicit native call stack and ad-hoc loop counter execution
---

# ADR 0029: Direct call은 explicit frame stack, loop는 verified latch counter로 집행

## Context

Resource closure는 non-recursive direct-call DAG, maximum call depth, maximum stack
bytes와 loop trip count를 계산한다. Runtime이 이 값을 단순 allocation hint로만
사용하고 C recursion이나 임의 branch count로 실행하면 verifier 상한과 실제
execution 사이에 별도 의미가 생긴다.

Pre-OS policy runtime은 평균 속도보다 다음 성질이 중요하다.

- policy call depth가 host 또는 target C stack과 무관하다.
- interruption 지점의 function, continuation과 resource consumption을 arena만으로
  설명할 수 있다.
- loop 상한은 compiler가 추측한 source construct가 아니라 verified CFG edge에
  결박된다.
- nested call과 loop도 하나의 instruction fuel을 공유한다.

## Decision

- `CALL_DIRECT`는 C recursion 없이 arena의 32-byte frame record를 push한다.
- Entry를 포함한 모든 active frame은 연속 frame-value stack과 explicit record를
  가진다.
- Continuation은 native return address가 아니라 verified instruction ID다.
- Argument와 return transfer는 v1.2에서 exact type의 scalar 또는 copy-only inline
  aggregate 전체 byte로 제한한다.
- 모든 function frame 크기는 8-byte 배수이고 stack closure는 frame-end padding을
  포함한다.
- Active function ID 중복을 거부해 verifier의 recursion 금지를 runtime에서도
  집행한다.
- Frame push 전에 verifier call depth와 maximum stack bytes를 검사한다.
- Instruction fuel은 call, callee, return과 caller에서 하나의 counter로 집행한다.
- Loop counter는 verified loop row마다 하나이며 latch-to-header backedge에서만
  감소한다.
- Header-to-body는 remaining count를 검사하고 external header entry는 counter를
  reset한다.
- Call, loop와 fuel 위반은 현재 verified function/instruction을 보존한 sealed fault
  receipt로 종료한다.
- Architecture, board와 product-specific 분기는 이 실행 경계에 추가하지 않는다.

## Consequences

- Policy call graph가 target C stack 크기나 ABI에 영향을 주지 않는다.
- Snapshot은 active frame depth와 stack bytes를 직접 보고할 수 있다.
- Verifier의 call-depth/stack/instruction closure와 실제 dispatch를 host gate에서
  수치로 비교할 수 있다.
- Inner loop는 outer iteration마다 external header entry에서 재활성화된다.
- Recursion, indirect call과 tail-call 최적화는 지원되지 않는다.
- Aggregate call transfer는
  {doc}`../contracts/language/ribos-bounded-aggregate-runtime-v1`의 결정론적 inline
  representation을 exact-copy한다.
- Opaque handle의 runtime consume/borrow state는 별도 ownership 계약이 닫힐 때까지
  실행 증거에 포함하지 않는다.
- Frame validation은 매 step에 bounded 추가 비용을 만든다. 성능 최적화는 동일
  invariant와 differential evidence를 보존해야 한다.
- Helper callback, terminal action과 recovery authority는 여전히 후속 runtime
  계층의 책임이다.

## 기각한 대안

### Native C recursion

Policy call depth를 target compiler ABI와 firmware stack에 결박하고 resumable
snapshot과 deterministic stack accounting을 어렵게 하므로 기각한다.

### Counter를 모든 backward branch에서 감소

Verified loop identity가 아닌 instruction ordering에 의존하고 nested CFG에서 어느
loop의 상한을 소비했는지 설명하기 어려우므로 기각한다.

### Header 진입마다 감소

초기 진입과 backedge를 구분하지 않으면 `N` trip의 정의가 흔들리고 inner loop
reactivation을 표현하기 어려우므로 기각한다.

### 평균 성능을 위한 hash 기반 loop state

Loop ID는 dense verified table이고 fixed array가 더 작은 공격 표면과 명확한
worst-case access를 제공하므로 기각한다.
