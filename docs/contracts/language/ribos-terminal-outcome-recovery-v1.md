---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/include/ribos/vm/runtime.h
  - language/ribos/vm/include/ribos/vm/helpers.h
  - language/ribos/vm/include/ribos/vm/terminal.h
  - language/ribos/vm/src/runtime/helpers.c
  - language/ribos/vm/src/runtime/storage.c
  - language/ribos/vm/src/runtime/terminal.c
  - language/ribos/vm/tests/terminal_runtime_tests.c
  - language/ribos/vm/tests/terminal_policy_error.rbs
  - language/ribos/vm/tests/terminal_journal.rbs
  - Makefile
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
  - unsealed Ribos entry RETURNED outcome
---

# Ribos terminal outcome와 fail-closed recovery v1 계약

## 목적

이 계약은 verified Ribos policy 실행이 다음 세 결과 중 정확히 하나로 끝나는 규칙을
고정한다.

| Outcome | action 수 | recovery notification |
| --- | ---: | ---: |
| `BootAction` | 정확히 1개 | 0회 |
| `PolicyError` | 0개 | 0회 |
| `VmFault` | 0개 | 최대 1회 |

`BootAction`은 Ribon Core가 이후 검토하고 소비할 boot intent다. VM은 action을
봉인하지만 OS entry jump, CPU quiesce, update commit과 실제 control transfer를
수행하지 않는다.

## Interpreter와 terminal 계층

Low-level interpreter의 `RETURNED`는 terminal outcome이 아니다. High-level
`ribos_vm_policy_execute_v1`은 다음 순서로 실행을 닫는다.

```text
fresh initialized storage
  -> terminal execution receipt initialize
  -> interpreter initialize
  -> helper execution initialize
  -> helper-aware bounded run
  -> entry Result 재검증
     + Ok(action)  -> sealed BootAction
     + Err(error)  -> typed PolicyError
     ` fault      -> sealed VmFault + authority revoke + recovery notify
```

Terminal helper의 callback 결과는 먼저 `ACTION_PENDING` intent가 된다. 기존 ISA가
요구하는 `BUILD_VARIANT`와 `RETURN` 같은 pure epilogue는 pending 뒤 실행할 수 있다.
Entry `Result.Ok` payload가 pending payload와 byte-for-byte 일치할 때만
`ACTION_SEALED`로 바뀐다. 따라서 seal 뒤에는 bytecode가 더 실행되지 않는다.

Pending 뒤 instruction fault, 다른 helper, 두 번째 terminal helper 또는
`Result.Err`가 관찰되면 pending action은 성공 outcome이 되지 않는다. Runtime은
`TERMINAL_ACTION` fault를 봉인하고 output payload를 zeroize한다.

## Terminal lifecycle

Arena terminal receipt의 stable state는 다음과 같다.

| State | 의미 |
| --- | --- |
| `EMPTY` | storage initialize 직후 sentinel |
| `EXECUTING` | action이나 terminal outcome 없음 |
| `ACTION_PENDING` | terminal helper 결과가 있으나 entry Result 검증 전 |
| `ACTION_SEALED` | 성공 outcome으로 봉인된 single-consume intent |
| `POLICY_ERROR` | policy가 명시적으로 반환한 typed error |
| `VM_FAULT` | catch 불가능한 fault와 recovery closure |
| `ACTION_CONSUMED` | Ribon Core caller가 intent를 한 번 소비함 |

State, outcome tag, type ID, context generation, journal count와 digest는 outcome region의
두 번째 256-byte record에 field-wise little-endian으로 저장한다. 첫 번째 256-byte
record는 helper execution snapshot이 소유한다. Native C padding이나 pointer는 arena에
기록하지 않는다.

Payload는 bounded output region에 복사한다. `BootAction`과 `PolicyError`의 public
pointer는 이 region을 borrow하며 storage reset, fault closure 또는 action consume
뒤에는 유효하지 않다.

## BootAction seal과 single consume

Action receipt digest는 다음 stable input을 SHA-256으로 결박한다.

- domain `RIBOS-TERMINAL-V1`
- artifact hash
- PreparedProgram binding digest
- context digest와 generation
- terminal helper stable ID와 action type ID
- payload length와 exact payload byte
- journal receipt count, sequence와 chain digest

Pointer, native structure padding과 secret byte는 digest input이 아니다.

`ribos_vm_boot_action_consume_v1`은 caller가 제시한 helper ID, type ID, generation,
payload pointer, size와 receipt digest를 arena seal과 다시 대조한다. 첫 성공에서
output region을 zeroize하고 state를 `ACTION_CONSUMED`로 바꾼다. 같은 action의 두 번째
호출은 `already-consumed`다.

Consume 성공은 실제 jump 성공이나 boot 성공 증거가 아니다. Action을 어떤
Ribon Core transaction에 적용할지는 이 계약 밖의 authority다.

## PolicyError

Entry `Result.Err`는 terminal helper가 pending되지 않은 경우에만 `PolicyError`가
된다. Runtime은 다음을 보존한다.

- verified error type ID
- enum payload에서 읽은 stable error code
- `RETURN` instruction source-map ID
- exact typed payload byte

PolicyError는 exception이나 catchable VM fault가 아니다. 명시적 policy 결과이며
factory recovery callback을 호출하지 않는다. 정책이 성공 경로를 전혀 갖지 않고
모든 경로에서 `Err`를 반환하는 것도 유효하다. 단, 존재하는 모든 `Ok` 경로에는
terminal action이 정확히 하나 있어야 한다.

## Durable helper receipt

`JOURNALED` helper는 callback 동안
`ribos_vm_helper_call_set_journal_receipt_v1`을 정확히 한 번 호출해야 한다. Receipt는
product가 canonicalize한 pointer-free digest와 다음 상태 중 하나를 가진다.

| State | 의미 |
| --- | --- |
| `COMMITTED` | callback이 완료한 durable transaction receipt |
| `PARTIAL` | durable effect 일부가 남았음 |
| `UNCERTAIN` | 외부 상태를 local evidence만으로 확정할 수 없음 |

Callback `OK`는 `COMMITTED` receipt만 허용한다. Receipt 누락, zero digest와 잘못된
state는 helper contract fault다. `POLICY_ERROR` 또는 `CONTRACT_FAULT`는 product가
보고한 `PARTIAL`이나 `UNCERTAIN` evidence를 보존할 수 있다.

Journal chain digest는 이전 chain, artifact/binding/context identity, helper와 source
identity, callback status, state, sequence와 product receipt digest를 결박한다.
Runtime은 chain count와 마지막 helper를 bounded terminal snapshot에 보존한다.

이 chain은 durable effect가 실제로 rollback되었다는 증명이 아니다. VM local handle,
slot과 output을 폐쇄해도 flash, network peer, secure storage 또는 device effect는
남을 수 있다. Recovery authority는 `PARTIAL`과 `UNCERTAIN`을 외부 reconciliation
입력으로 처리해야 한다.

## VmFault와 recovery closure

Interpreter 또는 helper가 fault receipt를 봉인하면 terminal 계층은 다음 순서를
따른다.

1. 모든 VM generation handle을 capacity 이하 순회로 revoke한다.
2. Drop callback은 최대 한 번 호출하며 실패해도 host binding을 지운다.
3. Helper execution state를 `FAULTED`로 바꾸고 callback authority를 닫는다.
4. Pending action과 output payload를 zeroize한다.
5. Fault, journal chain과 cleanup count의 bounded trace digest를 계산한다.
6. Fault record의 `recovery_notified`를 callback 전에 1로 바꾼다.
7. Terminal receipt를 `VM_FAULT`, `authority_revoked=1`로 봉인한다.
8. Product-owned factory recovery callback을 최대 한 번 호출한다.
9. Callback 반환과 무관하게 원래 `VmFault` outcome을 반환한다.

Factory recovery callback은 fault outcome을 성공으로 바꾸거나 VM에 재진입할 수
없다. Callback은 receipt pointer를 보존할 수 없다. 같은 storage로 execute를 다시
시도하면 terminal initialization이 `invalid-state`로 거부하므로 recovery callback도
두 번 호출되지 않는다.

VM이 가진 network/writer authority는 synchronous helper callback 수명에만 존재한다.
Fault 뒤 callback active state와 handle을 폐쇄하는 것이 VM-side revoke의 범위다.
이미 외부에 전달된 capability, hardware transaction 또는 durable effect까지
취소한다고 주장하지 않는다.

## Fixed fault receipt

`RibosVmFaultReceipt`는 pointer와 secret을 포함하지 않는 fixed-size process-local
value다.

- stable fault/subject/helper ID
- function, instruction과 detail
- 마지막 effect와 durability
- instruction/helper/input/output/operation/poll count
- elapsed nanosecond receipt
- artifact hash
- optional bounded trace digest

Trace digest는 fault closure와 journal chain의 identity다. Raw trace, callback pointer,
device address와 payload secret을 담지 않는다.

## Verifier와 runtime의 분담

Stage-2 verifier는 artifact byte에서 다음을 재도출한다.

- terminal helper는 entry function에만 존재한다.
- 각 `Ok` return path는 terminal helper를 정확히 한 번 지난다.
- `Err`와 `TRAP` path는 terminal helper를 지나지 않는다.
- terminal helper 뒤에는 pure result packaging과 `RETURN`만 남는다.

Runtime은 compiler와 verifier metadata를 그대로 승인하지 않고 실제 helper receipt,
entry tag, action type, payload byte와 terminal state를 다시 확인한다. Hostile
`no-action-success`, double action, fault-after-action과 double-consume은 runtime에서
fail closed한다.

## 증거 경계

다음 gate는 host VM unit evidence다.

```sh
make check-ribos-vm-terminal
make check-ribos-vm-faults
make check-ribos-vm-helpers
make check-ribos-verifier
```

이 증거는 single-consume action, PolicyError 분리, durable receipt chain, bounded fault
closure와 recovery-once 의미를 검증한다. 실제 Ribon boot transaction, Parus handoff,
network/flash product adapter, QEMU transfer 또는 hardware recovery 실행을 증명하지
않는다.
