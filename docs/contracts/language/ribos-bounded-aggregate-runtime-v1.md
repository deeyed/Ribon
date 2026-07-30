---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/ir/include/ribos/ir/analysis.h
  - language/ribos/ir/src/analysis.c
  - language/ribos/artifact/include/ribos/artifact/format.h
  - language/ribos/host/src/artifact_emitter.c
  - language/ribos/vm/include/ribos/vm/interpreter.h
  - language/ribos/vm/src/verifier.c
  - language/ribos/vm/src/runtime/interpreter.c
  - language/ribos/vm/src/runtime/storage_internal.h
  - language/ribos/vm/src/runtime/storage.c
  - language/ribos/vm/tests/aggregate_interpreter.rbs
  - language/ribos/vm/tests/calls_loops_interpreter_tests.c
  - language/ribos/vm/tests/verifier_tests.py
  - Makefile
tests:
  - make check-ribos-vm-aggregates
  - make check-ribos-vm-calls
  - make check-ribos-artifact
  - make check-ribos-verifier
  - make check
  - make docs
hardware:
  - none
supersedes:
  - aggregate opcodes without an executable runtime representation
---

# Ribos bounded aggregate runtime v1 계약

## 목적과 증거 경계

이 계약은 independently verified aggregate bytecode를 caller-owned arena 안에서
실행하는 단일 의미를 고정한다. List와 Dict는 artifact가 정한 capacity보다 커질 수
없고, struct와 variant는 host C ABI가 아닌 bytecode type/shape table의 layout을
따른다.

```text
verified type + shape + slot rows
                 |
                 v
       fixed inline representation
                 |
        +--------+---------+
        |                  |
        v                  v
deterministic value    fail-closed fault
```

이 계약의 증거는 host interpreter와 hostile artifact/runtime corpus다. Helper callback,
ownership-bearing handle의 dynamic consume/borrow, sealed `BootAction`, firmware,
QEMU와 physical hardware policy execution은 증명하지 않는다.

## 공통 representation 규칙

Aggregate value는 해당 slot의 verified `byte_size` 전체다. 별도 heap pointer, native
union, host `sizeof`, flexible array와 allocator metadata를 포함하지 않는다.

- 모든 multi-byte scalar field는 little-endian이다.
- type alignment은 1, 2, 4 또는 8이고 모든 offset 계산은 overflow를 검사한다.
- 새 aggregate를 만들기 전에 slot 전체를 zero한다.
- padding, 사용하지 않은 payload와 collection의 unused capacity는 zero다.
- aggregate `MOVE`, direct-call argument와 return은 slot 전체 byte를 exact-copy한다.
- source와 destination이 겹쳐도 정의된 `memmove` 의미를 가진다.
- slot initialized state는 전체 값이 완성된 뒤에만 관찰 가능하다.

Function frame은 최소 8-byte alignment를 가진다. 각 `frame_bytes`는 8의 배수이고
maximum stack closure는 frame-end padding을 포함한다. Policy IR, bytecode verifier와
runtime arena가 서로 다른 frame alignment를 선택할 수 없다.

## Array와 List

`Array[T, N]`은 `N * stride(T)` bytes의 inline payload다. Runtime length는 항상
capacity `N`이고 별도 header가 없다.

`List[T, N]`은 다음 layout이다.

| 영역 | 의미 |
| --- | --- |
| byte 0..3 | 저장된 length인 little-endian `u32` |
| padding | `T` alignment까지 zero |
| payload | `N`개의 `stride(T)` entry |
| tail padding | type alignment까지 zero |

`BUILD_LIST` operand 수는 capacity 이하여야 한다. Array는 operand 수가 capacity와
정확히 같아야 하고 List는 0부터 capacity까지 허용한다. 각 operand는 element type과
byte size가 정확히 같아야 한다.

`INDEX`는 unsigned index가 Array capacity 또는 List의 runtime length보다 작을 때만
element 전체 bytes를 복사한다. `COLLECTION_LENGTH`는 Array capacity 또는 verified
List length를 `u32`로 반환한다. 손상된 length와 out-of-range index는
`INVALID_VALUE`다.

## Dict와 FrozenMap

Dict와 FrozenMap은 hash table이 아니라 fixed-capacity sorted entry array다.

| 영역 | 의미 |
| --- | --- |
| byte 0..3 | 저장된 entry count인 little-endian `u32` |
| padding | entry alignment까지 zero |
| entries | key와 value를 가진 `capacity`개의 fixed-stride entry |
| unused entries | 전부 zero |

`BUILD_MAP`은 source pair 순서와 관계없이 key의 total order로 insertion-sort한다.
지원 key는 verifier가 ordered comparison을 허용한 unsigned/signed integer 또는
product enum scalar다. 같은 canonical key가 두 번 나타나면 last-wins나
first-wins를 선택하지 않고 `INVALID_VALUE`로 실패한다. 이 정책은 source order와
implementation hash seed가 결과 bytes를 바꾸지 않게 한다.

`INDEX`는 최대 `count`개의 entry를 stable order로 bounded linear search한다. Key가
있으면 value를, 없으면 instruction의 explicit default operand를 exact-copy한다.
Runtime count가 capacity를 넘거나 entry layout이 type row와 맞지 않으면
fail closed한다. Dynamic resize, tombstone, randomized hash와 iterator mutation은
없다.

## Struct

Struct field는 shape table의 declaration ordinal 순서로 배치한다. 각 field 시작을
field type alignment에 맞추고 마지막 크기를 struct 최대 alignment에 맞춘다.

`BUILD_STRUCT`는 모든 declaration field를 정확히 한 번, ordinal 순서로 받아야 한다.
`MEMBER`는 verified owner type과 ordinal을 다시 확인하고 field 전체 bytes만
반환한다. 이름 lookup, reflection, optional field와 native C structure cast는
허용하지 않는다.

## Option, Result와 enum

Tagged union은 byte 0의 unsigned tag와 최대 payload를 담는 inline value다. Payload
시작은 모든 variant payload의 최대 alignment에 맞춘다.

- Option tag는 `Some = 0`, `None = 1`이다.
- Result tag는 `Ok = 0`, `Err = 1`이다.
- user enum tag는 shape row에 명시되고 `0..255` 범위여야 한다.
- payload가 없는 variant의 payload 영역은 전부 zero다.
- 작은 payload 뒤 남은 union capacity도 zero다.

`BUILD_VARIANT`는 owner type, tag, payload count와 payload type을 shape/type table에
대조한다. `VARIANT_TAG`는 저장된 tag가 실제 variant인지 확인한 뒤 `u32`로
반환한다. `VARIANT_PAYLOAD`는 instruction이 요구한 tag가 저장된 tag와 같고 해당
payload가 존재할 때만 exact-copy한다. 잘못된 tag, 없는 payload와 payload 크기
불일치는 `INVALID_VALUE`다.

## Aggregate 비교와 direct call

같은 type ID의 copy-only aggregate equality는 canonical full-slot bytes를 비교한다.
모든 padding과 unused capacity가 zero이므로 논리적으로 같은 값은 같은 byte
representation을 가진다.

Direct call은 scalar와 aggregate를 구분한 별도 calling convention을 만들지 않는다.
Type ID와 byte size가 일치하는 initialized slot 전체를 aligned callee frame의
parameter slot로 복사하고, return도 caller result slot 전체를 복사한다. Frame을
pop하기 전에 return copy가 끝나야 한다.

이 규칙은 ownership-bearing opaque handle의 runtime duplication을 허가하는 계약이
아니다. Affine/linear handle state와 helper effect transition은 별도 계약이
정의될 때까지 실행 증거 밖에 있다.

## Fail-closed 조건

다음 조건은 부분 값, truncation 또는 fallback lookup을 만들지 않고 fault receipt로
끝난다.

- capacity를 넘는 List/Dict construction
- Array element 수 불일치
- duplicate Dict key
- collection length가 capacity보다 큼
- invalid index 또는 variant tag
- shape owner, ordinal, field/payload type 불일치
- type size, stride, payload offset 또는 alignment 불일치
- uninitialized source slot
- slot/frame/arena bounds를 벗어난 copy
- overflow하는 offset, length, stride 또는 stack 합
- 8-byte 정렬이 아닌 function frame

Fault가 난 instruction은 이미 소비된 instruction fuel에 포함된다. Aggregate
실패는 language exception으로 catch할 수 없고 policy가 다른 boot action으로
바꿀 수 없다.

## Gate

```sh
make check-ribos-vm-aggregates
```

Gate는 두 artifact를 사용한다.

- `aggregate_interpreter.rbs`: empty/partial/full List, empty/nonempty Dict, nested
  aggregate, struct, enum/Option/Result, stable map ordering과 deterministic zeroing
- `aggregate_lowering.rbs`: aggregate direct-call parameter/return과 8-byte frame
  alignment closure

Hostile 경로는 duplicate key, out-of-range index, corrupt runtime tag와 1-byte tag
범위를 넘는 artifact shape를 거부한다. 이 gate의 성공을 helper, ownership,
firmware, QEMU, physical hardware 또는 full boot policy 성공으로 확대하지 않는다.
