---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/host/tools/run.c
  - language/ribos/host/tests/
  - language/ribos/vm/
  - Makefile
tests:
  - make check-ribos-host-tools
  - make check-ribos-replay
  - make check-ribos-conformance
  - make check-ribos-hostile
  - make check-ribos-vm
  - make check
  - make docs
hardware:
  - none
supersedes:
  - separate Ribos reference-VM replay
---

# ADR 0034: Production VM core를 deterministic host replay에 직접 연결

## Context

Ribos target core에는 independent verifier, explicit runtime storage, portable
interpreter, generation handle, typed helper dispatch와 terminal recovery가 구현되어
있었다. 개별 C unit harness는 각 계층을 검증했지만 source부터 실제 production VM
entry까지 같은 실행을 반복하는 public host tool은 없었다.

별도 reference VM을 만들면 compiler와 test가 서로 동의하면서 target core 결함을
놓칠 수 있다. 반대로 host helper가 현재 시간, network와 filesystem을 직접 읽으면
동일 policy의 결과를 재현하거나 hostile fault를 비교하기 어렵다.

## Decision

- `ribosc`, `ribos-verify`, `ribos-run`을 공식 host tool 이름으로 사용한다.
- `ribos-run`은 `libribos-target-core.a`를 직접 링크하며 다른 VM을 구현하지 않는다.
- Product context는 versioned `.rbctx`, helper callback sequence는 versioned `.rbtr`
  little-endian fixture로 받는다.
- Context snapshot은 type/mode/phase/generation/range/payload를 하나의 SHA-256
  identity로 묶는다.
- Transcript는 artifact hash와 context identity를 결박하고 result, operation, poll,
  elapsed, journal receipt와 handle object identity를 순서대로 기록한다.
- Host runner는 unsigned local artifact만 승인하는 development authorizer를
  사용하며 production trust를 주장하지 않는다.
- Report는 path/pointer/time을 제외한 canonical key order와 final digest를 가진다.
- Compiler, verifier와 runtime의 resource closure를 같은 corpus에서 비교한다.
- 모든 ISA v1 opcode는 source-to-run conformance corpus에 포함한다.
- Mutation, truncation, coherent invalid artifact와 callback contract fault를 bounded
  hostile test로 실행한다.
- Source map은 diagnostic-only이며 의미 비교에서 identity/source field와 분리한다.

## Consequences

- Host replay 성공은 production target-core VM 코드가 실행되었다는 증거다.
- 같은 artifact/snapshot/transcript의 output, counter, journal과 trace digest를
  byte 단위로 비교할 수 있다.
- Helper nondeterminism은 암묵적 host state가 아니라 transcript 입력으로 드러난다.
- Host authorizer와 fixture helper는 product signature, rollback, 실제 장치 effect를
  대체하지 않는다.
- `ribos-run` 성공을 QEMU, network, OTA, firmware 또는 hardware 실행으로 부를 수
  없다.
- Source-map section 생략 artifact는 independent verifier 경로가 열릴 때까지 host
  compiler에서 발행하지 않는다.

## 기각한 대안

### 별도 reference VM

Target core와 다른 dispatch, counter 또는 fault 의미가 생겨 같은 버그를 검증하지
못하므로 기각한다.

### JSON 또는 native C fixture

JSON은 bounded wire range와 byte identity가 모호하고 native C layout은 endian,
padding과 host ABI에 종속되므로 기각한다.

### Replay 중 실제 network와 filesystem 사용

환경 state가 입력 계약 밖에 남고 동일 실행을 재현할 수 없으므로 helper transcript로
대체한다.

### Host replay 성공을 production authorization으로 사용

Development authorizer는 key, signature와 rollback authority가 없으므로 실행
신뢰결정으로 승격할 수 없다.
