---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/include/ribos/vm/helpers.h
  - language/ribos/vm/include/ribos/vm/interpreter.h
  - language/ribos/vm/src/runtime/helpers.c
  - language/ribos/vm/src/runtime/interpreter.c
  - language/ribos/vm/src/runtime/storage.c
  - language/ribos/vm/tests/handle_runtime_tests.c
  - docs/contracts/language/ribos-typed-helper-dispatch-v1.md
tests:
  - make check-ribos-vm-helpers
  - make check-ribos-vm-handles
  - make check-ribos-verifier
  - make check
  - make docs
hardware:
  - none
supersedes:
  - CALL_HELPER runtime placeholder
---

# ADR 0032: Helper는 Prepared binding을 통해 typed synchronous call로 실행

## Context

Stage-2 verifier와 PreparedProgram은 helper signature, reachable capability, effect,
budget와 typestate를 봉인하지만 이전 interpreter는 모든 `CALL_HELPER`를
`INVALID_STATE`로 종료했다. Stable ID를 raw callback index로 사용하거나 slot bytes를
embedder에 그대로 넘기면 다음 경계가 열린다.

- authorization 뒤 product table pointer나 callback이 바뀐다.
- type ID가 같지 않은 opaque object가 native pointer로 해석된다.
- consume 실패 뒤 source authority가 다시 사용된다.
- helper 전체와 stable-ID별 worst-case call bound가 실제 실행에서 집행되지 않는다.
- callback 재진입과 pointer 보존이 VM stack과 arena lifecycle을 깨뜨린다.
- 이미 발생한 effect와 fault receipt가 분리된다.

## Decision

- Base interpreter API는 helper authority를 갖지 않고 `CALL_HELPER`를 계속 fail
  closed한다.
- 별도 helper-aware step/run API만 synchronous callback을 실행한다.
- Runtime은 original product table이 아니라 PreparedProgram에 복사되고 digest로
  봉인된 stable-ID 정렬 binding table을 binary search한다.
- Operand와 result는 artifact type table, Prepared type semantics와 current slot
  state에서 다시 유도한다.
- Callback은 opaque `RibosVmHelperCall` accessor로만 argument, operation/poll budget과
  typed result를 다룬다.
- Opaque argument는 generation handle borrow/consume lease로 연결한다. Consume은
  callback 전에 source slot을 moved로 바꾸고 실패해도 old authority를 복원하지
  않는다.
- Total/per-helper call, input/output, operation/poll과 monotonic deadline을 runtime에서
  집행한다.
- Callback 전 arena state를 active로 봉인하고 재진입과 after-return accessor를
  거부한다.
- Callback result와 effect metadata를 pointer-free helper snapshot에 기록하고 fault
  receipt로 전파한다.
- Deadline은 cooperative poll과 callback 반환 뒤 검증이다. Generic VM이 native
  callback을 preempt한다고 주장하지 않는다.

## Consequences

- Artifact stable ID가 직접 native authority가 되지 않는다.
- Compiler를 신뢰하지 않아도 callback이 받는 type, ownership과 byte range는 verified
  tables에서 다시 만들어진다.
- Handle replacement와 terminal consume이 R13 generation lifecycle과 하나의 경로를
  사용한다.
- Callback은 heap, architecture, board와 OS에 중립인 C ABI 뒤에 남는다.
- Product helper가 operation/poll accessor를 우회한 외부 effect는 VM 보장의 범위
  밖이다.
- 이 결정은 typed helper execution까지 닫지만 terminal BootAction sealing, journal
  receipt chain, recovery notification과 product service adapter는 닫지 않는다.

## 기각한 대안

### Raw function pointer를 bytecode에 저장

Artifact를 target address space에 종속시키고 signature가 native authority를 위조할 수
있으므로 기각한다.

### Original product callback table을 실행 중 참조

Authorization 뒤 table mutation과 lifetime을 Prepared identity가 통제할 수 없으므로
기각한다.

### Slot bytes를 untyped callback 인수로 전달

Opaque provenance, exact `Result` layout와 consume ownership을 callback마다 다시
구현하게 되므로 기각한다.

### Async future와 event loop를 VM v1에 포함

Scheduler, heap, cancellation과 persistent continuation 의미가 helper closure를 크게
넓힌다. v1은 bounded synchronous callback과 explicit poll만 허용한다.
