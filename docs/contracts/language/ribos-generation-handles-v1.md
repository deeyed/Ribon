---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/include/ribos/vm/handles.h
  - language/ribos/vm/include/ribos/vm/helpers.h
  - language/ribos/vm/include/ribos/vm/prepared.h
  - language/ribos/vm/src/prepared_internal.h
  - language/ribos/vm/src/prepared.c
  - language/ribos/vm/src/runtime/handles.c
  - language/ribos/vm/src/runtime/helpers.c
  - language/ribos/vm/src/runtime/interpreter.c
  - language/ribos/vm/src/verifier.c
  - language/ribos/vm/tests/handle_runtime_tests.c
  - language/ribos/vm/tests/aggregate_ownership.rbs
  - language/ribos/vm/tests/hostile/noncopy_map_default.rbs
  - Makefile
tests:
  - make check-ribos-vm-handles
  - make check-ribos-vm-helpers
  - make check-ribos-vm-aggregates
  - make check-ribos-verifier
  - make check-ribos-schema
  - make check
  - make docs
hardware:
  - none
supersedes:
  - runtime-opaque ownership without generation validation
---

# Ribos generation handle과 runtime ownership v1 계약

## 목적과 증거 경계

이 계약은 Stage-2가 승인한 opaque handle provenance와 affine/linear ownership을
caller-owned runtime storage에서 방어하는 동적 의미를 고정한다.

```text
verified type semantics       embedder object
          |                         |
          v                         v
 PreparedProgram seal       process-local host entry
          |                         |
          +----------+--------------+
                     v
        arena generation record
                     |
                     v
       Ribos token (index, generation)
```

증거는 host unit/interpreter corpus와 sanitizer 실행이다. 실제 product helper
dispatch, firmware service, network/flash effect, boot transfer, QEMU와 physical
hardware execution을 증명하지 않는다.

## 세 저장 영역

### Ribos-visible token

Token은 정확히 8 bytes다.

| Offset | 형식 | 의미 |
| --- | --- | --- |
| 0..3 | little-endian `u32` | handle table index |
| 4..7 | little-endian `u32` | nonzero generation |

Token에는 pointer, callback, ownership bit, type ID와 host cookie가 없다. Token
decode가 성공해도 authority는 아니다. Record, host binding, expected type와
Prepared semantics를 모두 대조한 lookup만 authority를 만든다.

### Arena record

Storage plan의 handle region은 product `maximum_handles`개의 32-byte record다.

| Offset | 형식 | 의미 |
| --- | --- | --- |
| 0..3 | `u32` | generation |
| 4..7 | `u32` | lifecycle |
| 8..11 | `u32` | artifact type ID, inactive면 zero |
| 12..15 | `u32` | schema ownership |
| 16..19 | `u32` | successful non-copy move count |
| 20..31 | zero | reserved |

모든 field는 little-endian이다. Record는 pointer-free이며 storage image 밖으로
복사해도 trusted object를 노출하지 않는다.

### Host entry

Host entry는 caller가 제공한 process-local fixed array다. Trusted object pointer,
optional drop context/callback과 matching generation을 가진다. 이 structure는 stable
wire ABI, artifact section, arena image나 OS handoff가 아니다.

Host table capacity는 storage plan handle count와 정확히 같아야 한다. Record는
bound host entry 없이 available/borrowed/in-flight가 될 수 없다.

## Prepared type semantics

Prepare는 Stage-2 통과 뒤 모든 artifact type에 다음을 재귀적으로 계산한다.

- named type: selected schema ownership과 schema type class
- Array/List/Option: element ownership
- Dict/FrozenMap/Result: 구성 type 중 가장 강한 ownership
- struct/enum: 모든 field/payload 중 가장 강한 ownership

`copy < affine < linear` 순서를 쓴다. Cycle, unknown named type와 schema mismatch는
prepare 실패다. 결과 table은 Prepared workspace에 복사되고 binding identity에
포함된다. `ribos_prepared_program_type_semantics_v1()`만 이를 읽는다.

Handle create/lookup은 named `OPAQUE_HANDLE` type만 허용한다. Aggregate ownership은
interpreter의 whole-value 이동 판단에 쓰지만 aggregate 자체를 host handle
record로 만들지는 않는다.

## Lifecycle

```text
EMPTY --create--> AVAILABLE --borrow--> BORROWED
                         ^                 |
                         `------end--------'
AVAILABLE --consume--> IN_FLIGHT --finish--> REVOKED
                              `--replace--> AVAILABLE(new type)
AVAILABLE --revoke------------------------> REVOKED
non-empty --fault cleanup-----------------> REVOKED
```

`REVOKED` entry는 generation을 다시 증가시킨 create에서만 재사용한다.

### Create

- type은 PreparedProgram의 verified opaque type이어야 한다.
- trusted object는 null일 수 없다.
- linear type은 non-null drop callback을 요구한다.
- EMPTY 또는 REVOKED record와 완전히 clear된 host entry만 선택한다.
- generation을 증가시키고 available record와 host binding을 쓴 뒤 token을 반환한다.
- 빈 record가 없거나 generation이 wrap되면 `limit-exceeded`다.

### Lookup과 borrow

Lookup은 token generation, index bounds, `AVAILABLE`, expected type, ownership,
reserved zero와 host generation을 모두 검사한다. Snapshot에는 pointer가 없다.

Borrow begin은 `AVAILABLE -> BORROWED` 뒤 callback-local lease에 trusted pointer를
제공한다. Borrow end는 index, generation, type, ownership과 exact pointer가 모두
같을 때만 `BORROWED -> AVAILABLE`을 수행한다. Nested borrow, public lookup,
move와 consume은 borrowed record를 사용할 수 없다.

### Move

Copy handle move는 같은 token을 반환한다. Affine/linear move는 generation과 move
count를 증가시키고 destination token만 새 generation을 가진다. Source token은 즉시
stale다. Generation이나 move count가 wrap되면 이동하지 않고 실패한다.

Interpreter의 aggregate/direct-call move는 verified slot 전체 bytes를 먼저 복사한 뒤
non-copy source slot을 `MOVED`로 바꾼다. 이 규칙은 token이 없는 `None`과 error
variant에도 적용된다. Typed helper dispatch는 opaque consume 전에 token generation을
회전시키고 source slot도 `MOVED`로 바꾼다. Helper authority가 없는 base
interpreter의 aggregate 이동은 nested token generation을 회전시키지 않는다.

### Consume과 typestate

Consume begin은 copy type을 거부한다. Callback 전에 generation을 증가시키고
`IN_FLIGHT`를 기록한 뒤 lease를 반환한다. 따라서 callback이 보는 시점에는 입력
token이 이미 stale다.

Consume finish는 다음 둘 중 하나다.

- `TRANSFERRED`: embedder가 object authority를 외부로 넘겼다.
- `DROP`: drop callback을 최대 한 번 호출한다.

두 경우 모두 record와 host binding을 revoke한다. Drop 실패도 source token을
복원하지 않으며 `embedder-rejected`를 반환한다.

Consume replace는 lease와 exact in-flight record를 확인하고 다른 verified opaque
type의 trusted object로 바꾼다. Type ID가 v1 typestate이며 새 type ownership도
Prepared table에서 다시 읽는다. Source와 target type이 같거나 copy target,
linear target의 missing drop callback은 거부한다.

## Fault cleanup

Cleanup은 정확히 host table capacity 이하를 index 순으로 스캔한다.

- EMPTY/REVOKED host entry는 clear 상태로 만든다.
- AVAILABLE, BORROWED와 IN_FLIGHT entry는 drop callback을 최대 한 번 호출한다.
- Missing/inconsistent binding과 drop failure를 `drop_failures`에 센다.
- 실패가 있어도 모든 발견 record와 host binding을 revoke한다.
- report는 scanned, revoked, drop calls와 failures만 담고 pointer를 담지 않는다.

이 계약이 제한하는 것은 scan 수와 callback invocation 수다. Callback 자체의
instruction/time/I/O 상한은 helper execution contract가 소유한다. Drop은 메모리,
descriptor나 embedder object 정리 hook이며 이미 발생한 MMIO, flash, network 또는
device effect의 보상 transaction을 뜻하지 않는다.

## Aggregate와 조건부 ownership

`MOVE`, `BUILD_LIST`, `BUILD_MAP`, `BUILD_STRUCT`, `BUILD_VARIANT`, direct-call argument와
nested return은 각 non-copy operand slot 전체를 이동시킨다. Non-copy result를
꺼내는 `MEMBER`, `INDEX`, `VARIANT_PAYLOAD`는 owner aggregate를 이동시킨다.

Defaulted map lookup은 collection value와 fallback 중 하나를 런타임에 고른다. v1
verifier는 path-sensitive ownership join을 갖지 않으므로 result가 affine/linear이면
artifact를 `typestate-violation`으로 거부한다. Copy result만 default lookup을
사용할 수 있다.

## Fail-closed 조건

- zero/forged generation, out-of-range index
- record와 host generation 불일치
- expected type 또는 Prepared ownership/class 불일치
- reserved byte/field nonzero
- nested borrow, move/consume during borrow
- copy consume, double consume, stale lease
- generation/move-count wrap
- fixed capacity exhaustion
- inconsistent cleanup binding

이 실패는 language exception으로 바뀌지 않는다. Consume 시작 뒤 callback 실패가
있어도 old authority를 다시 available로 만들 수 없다.

## Gate

```sh
make check-ribos-vm-handles
make check-ribos-vm-helpers
```

Gate는 canonical token, stale/forged generation, wrong typestate, borrow exclusion,
copy/affine move, pre-callback consume invalidation, failed-drop non-revival,
consume-replace, capacity exhaustion, poisoned record와 bounded cleanup을 검사한다.
`aggregate_ownership.rbs`는 빈 `Option[Image]` 이동도 source slot 전체를
`MOVED`로 만드는지 확인하고 `noncopy_map_default.rbs`는 모호한 conditional move를
독립 verifier가 거부하는지 확인한다.
Helper gate는 실제 `Slot -> Image -> VerifiedImage` borrow/consume/replace와 terminal
consume을 fake embedder에서 실행한다. 자세한 범위는
{doc}`ribos-typed-helper-dispatch-v1`이 소유한다.
