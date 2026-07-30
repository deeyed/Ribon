---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/base/include/ribos/base/checked.h
  - language/ribos/base/src/checked.c
  - language/ribos/vm/include/ribos/vm/storage.h
  - language/ribos/vm/src/runtime/storage_internal.h
  - language/ribos/vm/src/runtime/storage.c
  - language/ribos/vm/include/ribos/vm/helpers.h
  - language/ribos/vm/tests/runtime_storage_tests.c
  - Makefile
tests:
  - make check-ribos-runtime-storage
  - make check-ribos-vm-scalar
  - make check-ribos-vm-calls
  - make check-ribos-vm-loops
  - make check-ribos-prepared-program
  - make check-ribos-resources
  - make check-ribos-verifier
  - make check-ribos-host-boundary
  - make check
  - make docs
hardware:
  - none
supersedes:
  - implicit interpreter heap and native value stack
---

# Ribos bounded runtime storage v1 계약

## 목적과 증거 경계

이 계약은 opcode를 실행하기 전에 `RibosPreparedProgram`의 exact resource closure를
caller-owned runtime arena로 변환한다. 대상은 VM control, direct-call frame, typed
slot value, loop/helper counter, opaque handle, aggregate scratch, terminal outcome,
output, fault receipt와 optional trace다.

```text
PreparedProgram
  + independent verifier report
  + effective product/mode limits
        |
        v
ribos_vm_runtime_size_v1
        |
        v
fixed-offset RibosVmStoragePlan
        |
        v
caller-owned aligned arena
```

이 구현은 opcode dispatch, helper callback, Ribon service adapter와 실제 boot action
생성을 포함하지 않는다. Gate 성공은 host memory-layout와 typed value unit 증거이며
firmware, QEMU 또는 physical hardware execution 증거가 아니다.

## Limit 교집합

Runtime에 적용하는 arena 상한은 다음 작은 값이다.

```text
effective arena limit
  = min(RIBOS_VM_RUNTIME_MAX_ARENA_BYTES_V1,
        prepared effective_limits.maximum_arena_bytes)
```

Generic absolute cap은 v1에서 128 MiB다. 이는 product가 사용할 기본값이 아니다.
Normal, recovery, provisioning 또는 diagnostic product는 generated descriptor로 더
작은 상한을 제공해야 한다. VM source에 product 이름이나 mode별 상수를 넣지 않는다.

Artifact가 봉인한 값 중 다음은 exact allocation 수치로 사용한다.

- independent verifier가 재계산한 maximum direct-call stack bytes
- independent verifier가 재계산한 maximum call depth
- artifact의 global slot, loop와 imported helper count
- verified slot table의 maximum runtime value storage bytes

다음은 product/mode가 선택한 feature capacity다.

- maximum handles
- maximum output bytes
- maximum trace records

Required arena가 effective limit보다 크면 size query부터 `limit-exceeded`로 종료한다.
Caller가 큰 buffer를 제공해도 product limit을 넓힐 수 없다. Caller buffer가 required
byte보다 작으면 initialization은 `arena-too-small`이며 partial runtime state를 만들지
않는다.

## Checked 산술

`ribos/base/checked.h`는 다음 두 산술 domain을 분리한다.

| Domain | Type | 용도 |
| --- | --- | --- |
| process-local storage | `size_t` | caller buffer와 C pointer range |
| architecture-neutral layout | `uint64_t` | plan offset, byte 수와 alignment |

Add, multiply, power-of-two align, range와 `uint64_t`에서 `size_t`로의 축소는 모두
명시적으로 검사한다. `offset + length`, `count * stride`와 마지막 alignment 중 하나라도
overflow하면 plan을 반환하지 않는다.

## 고정 region layout

모든 region 시작은 8-byte 정렬이다. Region은 다음 순서로 한 번씩 나타나며 순서를
바꾸려면 storage ABI version 변경이 필요하다.

| Index | Region | Count 근거 | Fixed stride 또는 size |
| ---: | --- | --- | ---: |
| 0 | control | 1 | 512 bytes |
| 1 | frame records | verified call depth | 32 bytes |
| 2 | frame values | 1 | verified stack bytes |
| 3 | slot states | global slot count | 8 bytes |
| 4 | loop counters | loop row count | 8 bytes |
| 5 | helper counters | helper import count | 16 bytes |
| 6 | handle records | product handle cap | 32 bytes |
| 7 | aggregate scratch | maximum value가 있으면 1 | maximum value bytes |
| 8 | outcome state | 1 | 256 bytes |
| 9 | outcome/helper output | output cap이 있으면 1 | product output cap |
| 10 | sealed fault state | 1 | 160 bytes |
| 11 | diagnostic trace | product trace cap | 32 bytes |

마지막 region 끝도 8-byte 정렬해 `required_bytes`를 만든다. Region이 비어 있어도
ordering과 aligned offset은 유지하고 count와 byte size는 zero다.

Control region 앞부분은 magic, storage ABI, initialization flag, required byte,
effective limit, entry function, Prepared binding digest와 12개 region descriptor를
little-endian으로 기록한다. 뒤 고정 영역은 instruction/helper/operation/poll counter,
stack cursor, frame depth와 interpreter state를 위한 공간이다. Interpreter state에는
current function/block/instruction ID, return slot, frame base, consumed count와 immutable
context generation/type/digest가 들어간다. Native pointer와 C structure image는
저장하지 않는다.

Frame record는 direct-call interpreter가 실제로 push/pop하는 bounded control
record다. Handle record는 {doc}`ribos-generation-handles-v1`의 pointer-free
generation/lifecycle/type/ownership record로 사용한다. Trusted pointer와 drop
callback은 arena가 아닌 caller-owned host table에만 있다. Outcome state의 256
bytes는 {doc}`ribos-typed-helper-dispatch-v1`이 helper execution identity, cumulative
budget과 마지막 effect receipt로 사용한다. Output region은 terminal executor가
사용할 reservation이고 trace는 optional diagnostic record다.
Fault region의 첫 152 bytes는 stable receipt field와 artifact/trace digest이고 마지막
8 bytes는 seal과 recovery-notified state다. Public `RibosVmFaultReceipt`의 native
padding이나 reserved word는 arena에 복사하지 않는다. Storage v1 API는 opcode,
handle transition 또는 recovery callback을 직접 dispatch하지 않는다.

## Frame와 slot

Frame value region 크기는 verifier가 artifact에서 재계산한 maximum stack bytes와
정확히 같다. 개별 function frame의 slot offset, size와 alignment도 verified function
및 slot table 값을 사용한다. Non-copy source는 exact transfer 뒤 slot state를
`MOVED`로 바꾸며 diagnostic poison mode에서는 value bytes도 poison한다.

32-byte frame record는 function ID, continuation instruction ID, caller return slot,
frame base와 frame size를 explicit little-endian field로 가진다. Entry record의
continuation과 return slot은 invalid sentinel이고 nested record에는 둘 다 있어야
한다. Active record의 base와 size는 빈틈 없이 stack cursor까지 이어지고 top record는
current function/frame과 일치한다.

Slot state는 artifact global slot ID로 index한다. ISA v1에서 slot은 한 function에만
속하고 recursive direct-call graph가 금지되므로 동시에 활성인 서로 다른 function은
서로 다른 global slot range를 사용한다. 같은 function을 나중에 다시 호출할 때에는
`ribos_vm_storage_reset_frame_v1`이 그 function slot-state range를 초기화한다.

Stable slot state는 다음 세 값뿐이다.

```text
UNINITIALIZED -> INITIALIZED -> MOVED
```

Uninitialized 또는 moved slot read는 `invalid-state`다. Write는 artifact slot의 exact
type byte size만 허용하고 initialized state를 만든다. Frame reset은 exact function
frame bytes와 그 function의 slot-state range만 변경한다.

## 실행값 encoding

Artifact wire table read와 runtime value access는 별도 코드 경로다.

- Artifact section과 row는 `language/ribos/artifact` reader가 검증한다.
- Runtime scalar slot은 `runtime/storage.c`의 value little-endian helper가 읽고 쓴다.
- Native C union, packed cast, unaligned integer dereference와 host-endian copy를
  사용하지 않는다.
- `bool`은 한 byte의 0 또는 1이다.
- Signed/unsigned integer는 type row와 동일한 8, 16, 32 또는 64 bit width만 허용한다.
- Narrow unsigned와 signed write는 범위를 검사한다.
- Signed read는 explicit sign extension을 사용한다.
- Aggregate와 opaque value의 generic byte copy도 exact verified slot size만 허용한다.

Host pointer width, native alignment와 C enum size는 Ribos value의 byte 의미를 바꾸지
않는다.

## 초기화와 validation

정상 호출 순서는 다음과 같다.

```text
runtime_size(prepared)
  -> caller allocates exact or larger aligned bytes
  -> storage_initialize(prepared, exact plan, arena)
  -> storage_validate(prepared, storage, arena capacity)
  -> later interpreter lifecycle
```

Initialization은 plan을 PreparedProgram에서 다시 계산하고 caller plan을 field별로
대조한다. Native padding을 `memcmp` 권위로 사용하지 않는다. Arena를 초기화한 뒤
Prepared binding digest와 canonical region descriptor를 control region에 기록한다.
Validation도 호출자가 제공한 PreparedProgram에서 plan을 다시 계산해 control bytes와
대조한다.

Loop counter는 verified loop trip count로 초기화한다. Function call과 latch가 아닌
header entry는 해당 function/loop counter를 reset하고 verified latch-to-header
backedge만 1을 감소시킨다. Header-to-body는 remaining count가 nonzero인지 검사한다.
Helper counter row는 imported stable helper ID와 entry function의 verified helper별
upper bound를 가진다. 전체 instruction/helper와 product operation/poll counter는
control region에 초기화한다.

## Diagnostic poison

`RIBOS_VM_STORAGE_INITIALIZE_DIAGNOSTIC_POISON`은 optional host/diagnostic mode다.

- 초기 frame value, aggregate scratch와 output byte를 `0xa5`로 채운다.
- moved slot value를 `0xdd`로 채운다.
- state machine과 bounds check는 poison 여부와 무관하게 항상 실행한다.

Poison은 보안 경계나 memory safety proof가 아니다. Production에서 poison을 끄더라도
uninitialized/moved state read는 계속 거부한다.

## Fail-closed 조건

다음 입력은 runtime storage를 만들거나 읽지 못한다.

- invalid 또는 mutated PreparedProgram
- product cap을 넘는 required arena
- `size_t`로 표현할 수 없는 required byte
- overflow된 count, offset, alignment 또는 pointer range
- 8-byte 미정렬 arena
- one-byte라도 작은 arena
- 다른 PreparedProgram 또는 변조된 plan
- binding digest, control header 또는 region table mismatch
- function 밖 slot, frame 밖 slot range와 잘못된 frame base
- type kind/bit width/byte size mismatch
- uninitialized 또는 moved slot read
- scalar range 밖 write와 canonical하지 않은 bool read

## Gate

```sh
make check-ribos-runtime-storage
```

Gate는 tracked `.rbs` fixture를 host compiler로 artifact로 만들고 authorization,
Stage-1/Stage-2 preparation 뒤 다음을 검사한다.

- repeated query의 deterministic required byte와 region ordering
- exact-size success, one-byte-small failure와 misaligned pointer failure
- modified plan과 control mutation failure
- checked add/multiply/align overflow
- generic cap보다 작은 product cap 적용
- product cap이 required byte보다 한 byte 작을 때 size-query rejection
- uninitialized, initialized와 moved slot transition
- unsigned, signed와 bool type/width check
- explicit little-endian scalar bytes
- optional diagnostic poison state

`make check-ribos-vm-calls`와 `make check-ribos-vm-loops`는 이 layout 위에서 explicit
frame push/pop, exact stack cursor, loop trip 소진과 external-entry reset을 실행한다.

Storage gate 자체는 compiler와 verifier gate를 선행하지만 interpreter instruction
semantics, helper dispatch, production signature/rollback, Ribon boot product linkage,
QEMU와 hardware 실행을 증명하지 않는다.
