---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/include/ribos/vm/helpers.h
  - language/ribos/vm/include/ribos/vm/interpreter.h
  - language/ribos/vm/src/prepared.c
  - language/ribos/vm/src/runtime/helpers_internal.h
  - language/ribos/vm/src/runtime/helpers.c
  - language/ribos/vm/src/runtime/interpreter.c
  - language/ribos/vm/src/runtime/storage_internal.h
  - language/ribos/vm/src/runtime/storage.c
  - language/ribos/vm/tests/handle_runtime_tests.c
  - language/ribos/vm/tests/check_interpreter_boundary.py
  - Makefile
tests:
  - make check-ribos-vm-helpers
  - make check-ribos-vm-terminal
  - make check-ribos-vm-faults
  - make check-ribos-vm-handles
  - make check-ribos-schema
  - make check-ribos-verifier
  - make check-ribos-host-boundary
  - make check
  - make docs
hardware:
  - none
supersedes:
  - CALL_HELPER fail-closed placeholder execution
---

# Ribos typed helper dispatch v1 계약

## 목적과 증거 경계

이 계약은 verified `CALL_HELPER` instruction을 product-generated synchronous
callback에 연결하는 architecture-neutral target runtime 의미를 고정한다.

```text
authorized artifact
        |
        v
independent verifier
        |
        v
PreparedProgram
  - copied artifact
  - copied helper descriptors
  - copied callback table
  - sealed type semantics
        |
        v
helper-aware interpreter
        |
        v
typed callback-local RibosVmHelperCall
        |
        +--> typed value or policy error
        +--> generation handle create/replace/consume
        `--> fail-closed helper receipt
```

이 계약의 evidence class는 host compiler가 만든 reference artifact와 fake embedder를
사용하는 unit execution이다. 실제 Ribon service, MMIO, network, OTA, flash
transaction, boot jump, QEMU와 physical hardware helper 실행은 증명하지 않는다.

## 실행 전 binding

Artifact에는 callback pointer가 아니라 stable helper ID와 helper import identity만
있다. Authorization 뒤 prepare는 다음을 수행한다.

1. selected product schema와 artifact schema digest를 대조한다.
2. independent Stage-1/Stage-2 verifier를 실행한다.
3. stable ID 오름차순 execution descriptor와 callback을 caller workspace에 복사한다.
4. callback address를 제외한 canonical helper execution digest를 authorization
   receipt와 대조한다.
5. schema의 signature, capability, typestate와 execution descriptor의 transition,
   effect와 durability가 일치하는지 검사한다.

Runtime dispatch는 original product table을 따라가지 않는다. PreparedProgram이 복사한
table을 binary search하고 그 copied callback만 호출한다. 실행 environment의 helper
contract identity도 같은 digest여야 한다.

Schema의 `CONSUME` parameter는 v1에서 정확히 하나의 typestate transition parameter여야
한다. 그 parameter type은 non-copy `OPAQUE_HANDLE`이어야 한다. 이 제약은
PreparedProgram 생성 전에 다시 검사한다.

## Helper environment와 시작

`RibosVmHelperEnvironment`는 process-local embedder와 generation handle host table을
묶는다. Pointer는 artifact, arena 또는 receipt에 저장하지 않는다.

Helper-aware 실행은 다음 순서를 요구한다.

```c
ribos_vm_storage_initialize_v1(...);
ribos_vm_interpreter_initialize_v1(...);
ribos_vm_helper_execution_initialize_v1(...);
ribos_vm_interpreter_run_with_helpers_v1(...);
```

Initialization은 context generation/type/digest, selected mode/phase, granted
capability, helper digest와 monotonic execution deadline을 arena의 256-byte helper
snapshot에 field-wise little-endian encoding으로 봉인한다. Native C structure image를
복사하지 않는다.

Base `ribos_vm_interpreter_step_v1()`과 `run_v1()`은 helper authority를 받지 않는다.
그 API에서 `CALL_HELPER`는 계속 fail closed한다. Product callback을 허용하는 API는
명시적인 `step_with_helpers_v1()`과 `run_with_helpers_v1()`뿐이다.

## Dispatch 전 검사 순서

한 `CALL_HELPER`는 callback 진입 전에 다음을 모두 통과해야 한다.

1. helper execution snapshot이 `READY`이고 callback-active가 아니다.
2. context와 environment가 initialization 때 봉인한 identity와 같다.
3. stable ID가 PreparedProgram copied table에 존재한다.
4. artifact result와 operand slot이 verified current function에 속한다.
5. 모든 operand slot이 initialized다.
6. Prepared type semantics에서 type ID, byte size, ownership과 schema class를 얻는다.
7. required capability가 granted bitmap의 부분집합이다.
8. selected mode와 phase bit가 descriptor mask에 있다.
9. per-helper와 global input/output 상한 안이다.
10. global helper counter와 stable-ID별 worst-case counter를 감소시킬 수 있다.
11. execution, global-helper와 per-helper deadline의 최소값이 유효하다.

Descriptor mismatch, overflow, counter exhaustion과 deadline 위반은 callback을 호출하지
않고 helper subject `VmFault`를 봉인한다.

## Typed argument와 result

Callback은 `RibosVmHelperCall`의 field를 직접 볼 수 없다. 다음 versioned accessor만
사용한다.

- call metadata와 argument metadata 조회
- exact expected type ID를 지정한 value copy
- exact opaque type ID를 지정한 callback-local trusted object borrow
- typed success value 또는 opaque success handle 설정
- typed `Result.Err` policy payload 설정
- consumed authority의 explicit external transfer 표시

일반 value는 verified slot의 exact byte size로 복사한다. `Result[T, E]`는 artifact type
table의 payload offset과 `T`/`E` byte size를 사용하며 tag `0`은 success, tag `1`은
policy error다. Callback이 임의 result size, 다른 type ID, 두 번째 result 또는
descriptor transition과 맞지 않는 result class를 쓰면 contract fault다.

Opaque token은 `(u32 index, u32 generation)`이다. Runtime은 callback 전에 borrow 또는
consume lease를 연다.

- Borrowed object pointer는 callback 반환까지 유효하다.
- Consume은 callback 전에 source generation을 회전하고 source slot을 `MOVED`로
  바꾼다.
- `REPLACE` success는 같은 in-flight record를 새 verified opaque type으로 바꾼다.
- `CONSUME`와 `TERMINAL_CONSUME`는 source authority를 복원하지 않는다.
- Callback이 external transfer를 표시하지 않으면 source drop callback을 최대 한 번
  호출한다.
- Callback이 생성했지만 commit되지 않은 pending handle은 반환 경로에서 정리한다.

## Resource와 deadline

Runtime은 다음 counter를 별도로 집행한다.

| Counter | 소비 시점 |
| --- | --- |
| instruction | opcode 해석 전 |
| total helper calls | callback 진입 전 |
| stable-ID helper calls | callback 진입 전 |
| input bytes | verified arguments marshal 전 |
| output bytes | verified result reservation 전 |
| operations | callback이 external operation을 시작하기 전 |
| polls | callback의 각 bounded poll 전 |

Operation과 poll은 callback이 accessor로 명시적으로 소비해야 한다. Product helper가
accessor를 거치지 않고 외부 작업을 수행하면 VM은 그 작업을 계수하거나 검증했다고
주장할 수 없다.

Deadline clock은 monotonic nanoseconds다. Callback 시작 전, 각 poll과 callback 반환
직후 검사한다. Synchronous C callback을 VM이 강제로 preempt하지는 않는다. 따라서
장시간 callback에 대한 hard watchdog/reset은 product embedder와 platform watchdog
계층의 책임이다. VM의 deadline receipt는 cooperative poll과 after-return 검증이다.

## Non-reentrancy와 lifetime

Callback 진입 전에 arena state를 `CALLBACK_ACTIVE`로 바꾼다. 같은 execution에 대한
helper-aware step/run 재진입은 instruction을 소비하지 않고 `invalid-state`다.

`RibosVmHelperCall`, argument trusted pointer와 accessor view는 callback-local이다.
Runtime은 callback 반환 전에 call을 inactive로 바꾼다. 보존한 call pointer를 나중에
accessor에 전달하면 `invalid-state`다. Callback은 VM이나 helper API가 자신을 다시
호출하도록 만들 수 없다.

## Effect receipt와 fault

256-byte helper snapshot은 cumulative counter와 마지막 call receipt를 가진다.

- helper stable ID
- callback status
- effect와 durability
- handle transition과 result kind
- input/output bytes
- operation/poll count
- callback duration
- monotonic receipt sequence

Callback 전 rejection도 known helper descriptor가 있으면 fault receipt를 남긴다.
Callback 진입 뒤 budget, deadline, handle 또는 result violation은 helper snapshot을
`FAULTED`로 바꾸고 interpreter의 sealed `RibosVmFaultReceipt`에 helper ID, effect,
durability와 cumulative resource count를 복사한다.

Policy `Result.Err`는 VM fault가 아니다. Typed error payload를 기록하고 policy CFG가
계속 실행한다. Contract fault, capability/mode violation, resource exhaustion과
embedder failure는 language에서 catch할 수 없다.

`JOURNALED` helper는 callback 동안 canonical product receipt digest와
`COMMITTED`, `PARTIAL` 또는 `UNCERTAIN` state를 설정해야 한다. `OK` callback에는
`COMMITTED`만 허용한다. Receipt chain, terminal action seal과 fault recovery 의미는
{doc}`ribos-terminal-outcome-recovery-v1`이 소유하며 external effect rollback을
주장하지 않는다.

## Gate

```sh
make check-ribos-vm-helpers
make check-ribos-vm-terminal
make check-ribos-vm-faults
make check-ribos-vm-handles
make check-ribos-verifier
make check-ribos-host-boundary
make docs
```

Host fake embedder gate는 다음을 실행한다.

- `Slot -> Image -> VerifiedImage -> BootAction` 정상 typed path
- `image.verify` policy error와 recovery helper path
- callback 중 interpreter 재진입 거부
- callback 반환 뒤 retained call accessor 거부
- exact operation/poll accounting
- per-helper operation overrun의 sealed helper fault
- missing granted capability의 callback 전 거부

이 gate 결과를 product service integration, firmware execution 또는 hardware 성공으로
승격해서는 안 된다.
