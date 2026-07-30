---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/include/ribos/vm/terminal.h
  - language/ribos/vm/src/runtime/terminal.c
  - language/ribos/vm/src/runtime/storage.c
  - language/ribos/vm/src/runtime/helpers.c
  - language/ribos/vm/src/verifier.c
  - language/ribos/vm/tests/terminal_runtime_tests.c
  - docs/contracts/language/ribos-terminal-outcome-recovery-v1.md
tests:
  - make check-ribos-vm-terminal
  - make check-ribos-vm-faults
  - make check-ribos-vm-helpers
  - make check-ribos-verifier
  - make check
  - make docs
hardware:
  - none
supersedes:
  - unsealed terminal helper result
  - mandatory policy success-path existence
---

# ADR 0033: Entry Result 검증 뒤 BootAction을 봉인하고 fault는 recovery로 폐쇄

## Context

Ribos interpreter와 typed helper dispatch는 verified bytecode를 bounded 실행할 수
있었지만 entry `RETURNED`는 아직 boot intent가 아니었다. Terminal helper callback이
반환한 값을 곧바로 action으로 노출하면 다음 문제가 남는다.

- terminal helper 뒤 epilogue fault가 발생해도 action이 성공으로 보인다.
- action이 두 번 생성되거나 소비될 수 있다.
- `PolicyError`와 runtime `VmFault`가 같은 실패 class로 섞인다.
- durable helper effect가 발생한 뒤 local rollback을 전체 rollback으로 오인한다.
- fault 뒤 handle, callback과 recovery notification authority가 열려 있다.

Stage-2 verifier는 모든 `Ok` 경로의 terminal helper 수를 정적으로 검사하지만
runtime의 실제 callback result, arena byte와 external effect receipt를 대신할 수
없다.

## Decision

- Low-level interpreter `RETURNED`와 public policy outcome을 분리한다.
- Terminal helper callback 결과는 `ACTION_PENDING`으로 복사한다.
- Entry `Result.Ok`의 verified action type과 exact payload가 pending intent와
  일치할 때만 `ACTION_SEALED`로 바꾼다.
- `BootAction`은 receipt-bound single-consume intent이며 실제 control transfer는
  Ribon Core가 소유한다.
- Entry `Result.Err`는 terminal action이 없을 때 typed `PolicyError`가 된다.
- 정책이 성공 경로를 반드시 하나 가져야 한다는 verifier 제한은 제거한다. 존재하는
  성공 경로만 action 정확히 하나를 요구한다.
- Journaled helper는 product receipt digest와
  `COMMITTED`/`PARTIAL`/`UNCERTAIN` state를 callback에서 제출한다.
- Runtime은 receipt를 hash chain으로 보존하지만 external effect rollback을
  주장하지 않는다.
- Fault에서는 handle cleanup, helper authority 폐쇄, pending/output zeroization,
  fixed receipt와 trace digest seal을 recovery callback보다 먼저 완료한다.
- `recovery_notified` bit를 callback 전에 기록해 같은 sealed fault의 callback을
  최대 한 번으로 제한한다.
- Policy exception과 catch는 추가하지 않는다.

## Consequences

- Compiler와 verifier가 승인한 `Ok` 경로도 runtime action seal을 다시 통과한다.
- Pure result-packaging epilogue는 pending intent 뒤 실행할 수 있지만 seal 뒤
  bytecode는 실행되지 않는다.
- PolicyError, VmFault와 BootAction의 수명과 권한이 명시적으로 분리된다.
- Fault-after-action, no-action-success, double-action과 double-consume이 fail
  closed한다.
- Recovery는 VM-side authority를 회수하지만 이미 발생한 flash/network/device
  effect를 되돌렸다고 주장하지 않는다.
- Terminal snapshot과 helper snapshot은 arena outcome region의 독립 fixed record를
  사용하고 native pointer를 저장하지 않는다.
- 새 API는 architecture, board, OS와 Ribon product service에 의존하지 않는다.

## 기각한 대안

### Terminal helper callback에서 즉시 jump

Entry Result 검증, action receipt와 Ribon Core transaction을 우회하고 helper callback
수명 안에 non-returning authority를 넣으므로 기각한다.

### Interpreter RETURN 값을 그대로 public outcome으로 사용

Pending action의 provenance, exact payload 대조와 single-consume state를 봉인할
위치가 없으므로 기각한다.

### VM fault를 policy가 catch

Verifier invariant, capability failure와 embedder contract fault가 policy control-flow로
복귀해 fail-closed 경계를 약화하므로 기각한다.

### 모든 durable helper에 generic rollback 적용

Flash, remote network state와 device effect의 rollback 의미를 generic VM이 알 수
없다. Receipt chain과 partial-state evidence만 보존하고 product recovery가
reconcile하도록 한다.

### 정책에 최소 한 개의 성공 경로를 강제

제품 상태에 따라 모든 경로에서 typed `PolicyError`를 반환하는 정책도 유효하다.
보안 요구는 성공 경로의 존재가 아니라, 존재하는 각 성공 경로의 action 수가 정확히
하나라는 점이므로 기각한다.
