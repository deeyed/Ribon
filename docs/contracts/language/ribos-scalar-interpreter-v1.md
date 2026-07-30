---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/include/ribos/vm/interpreter.h
  - language/ribos/vm/src/runtime/interpreter.c
  - language/ribos/vm/src/runtime/storage_internal.h
  - language/ribos/vm/tests/scalar_interpreter.rbs
  - language/ribos/vm/tests/scalar_interpreter_tests.c
  - language/ribos/vm/tests/check_interpreter_boundary.py
  - Makefile
tests:
  - make check-ribos-vm-scalar
  - make check-ribos-vm-calls
  - make check-ribos-vm-loops
  - make check-ribos-runtime-storage
  - make check-ribos-verifier
  - make check-ribos-host-boundary
  - make check
  - make docs
hardware:
  - none
supersedes:
  - implicit scalar opcode execution semantics
---

# Ribos portable scalar interpreter v1 계약

## 목적과 증거 경계

이 계약은 `RibosPreparedProgram`의 scalar opcode와 direct control-flow를 caller-owned
arena에서 실행하는 target-neutral interpreter 의미를 고정한다. Direct-call frame과
loop counter의 추가 의미는 {doc}`ribos-bounded-calls-loops-v1`이 소유한다.

```text
PreparedProgram + immutable context + initialized arena
                           |
                           v
               portable switch interpreter
                           |
              +------------+------------+
              |                         |
              v                         v
           RETURNED                  FAULTED
```

이 경계의 `RETURNED`는 내부 함수 반환이고 sealed `BootAction`이 아니다. `FAULTED`는
arena에 fixed receipt를 봉인하지만 factory recovery callback을 호출하지 않는다.
Helper callback과 public full-policy outcome은 후속 runtime 계층의 책임이다.
Aggregate·collection의 추가 의미는
{doc}`ribos-bounded-aggregate-runtime-v1`이 소유한다. 따라서 이 계약의 host gate는
firmware, QEMU, 실제 boot transfer 또는 physical hardware 실행 증거가 아니다.

## Public incremental API

`ribos/vm/interpreter.h`는 다음 versioned API만 노출한다.

| API | 의미 |
| --- | --- |
| `initialize_v1` | entry frame, immutable context identity와 첫 verified PC 결박 |
| `step_v1` | 정확히 한 instruction의 fuel 검사·감소·dispatch |
| `run_v1` | 남은 fuel보다 커질 수 없는 반복으로 내부 terminal까지 실행 |
| `snapshot_v1` | pointer-free control state 복사 |
| `fault_v1` | 봉인된 fixed fault receipt 복사 |

Caller는 artifact row, arena control byte 또는 internal storage function으로 PC를
설정하지 않는다. Internal storage API는 interpreter와 적대적 unit test만 사용한다.

Interpreter state는 다음 단방향 전이를 가진다.

```text
EMPTY -> READY -> RUNNING -> RETURNED
                         `-> FAULTED
```

`RETURNED`와 `FAULTED` 뒤의 `step`은 `already-consumed`다. 같은 arena에서 재실행하려면
storage initialization부터 다시 수행한다.

## Context 결박

Initialize는 policy entry가 정확히 한 parameter를 가지고 그 slot의 type ID와 byte
size가 `RibosVmContext`와 일치할 때만 성공한다. v1 scalar engine의 context slot은
최대 8 bytes다.

다음 identity는 arena control region에 복사한다.

- nonzero context generation
- context type ID
- SHA-256 context digest

매 step 전에 caller context의 descriptor와 byte SHA-256을 다시 검사한다. Generation,
type, digest 또는 borrowed byte가 달라지면 instruction fuel을 소비하지 않고
`digest-mismatch`를 반환한다.

## PC와 dispatch invariant

PC는 native pointer, byte offset 또는 computed-goto label이 아니라 verified global
instruction ID다. 매 dispatch에서 다음을 재확인한다.

- current function, block와 instruction row가 존재한다.
- row의 stable ID가 control state와 같다.
- block owner가 current function이다.
- instruction owner가 current block이다.
- instruction flags와 reserved field가 zero다.
- result와 operand slot이 current function과 verified type layout에 속한다.
- fallthrough instruction이 같은 block에 속하거나 direct branch가 verified target
  block의 첫 instruction으로 이동한다.

Interpreter는 C `switch`로 opcode를 dispatch한다. Architecture macro, computed goto,
JIT/AOT와 target별 fast path는 이 계약에 없다.

## Fuel 의미

Instruction fuel은 opcode decode와 side effect 전에 검사하고 정확히 1 감소한다.

```text
remaining == 0
    -> opcode를 읽거나 실행하지 않음
    -> INSTRUCTION_BUDGET receipt, consumed 불변

remaining > 0
    -> remaining -= 1
    -> consumed += 1
    -> instruction decode와 dispatch
```

Fault를 발생시킨 유효 instruction도 한 번 dispatch되었으므로 consumed count에
포함된다. `run_v1`의 host loop 자체는 fuel을 만들지 않으며 초기
`remaining + 1`보다 많이 `step`하지 않는다.

## v1 scalar opcode

이 계약이 실행하는 opcode는 다음과 같다.

| Opcode | Runtime 의미 |
| --- | --- |
| `PARAMETER` | context bytes를 exact entry slot에 복사 |
| `CONST_UNIT` | zero-byte unit slot 초기화 |
| `CONST_BOOL` | immediate 0 또는 1 |
| `CONST_INTEGER` | typed 8/16/32/64-bit 정수, 범위 검사 |
| `CONST_STRING` | constant ID와 byte length token |
| `CONST_SYMBOL` | constant ID token, 8-byte opaque면 length도 포함 |
| `MOVE` | 같은 type ID의 exact value bytes 복사 |
| `CHECKED_UNARY` | `not`, unary `+`, unary `-`, bitwise `~` |
| `CHECKED_BINARY` | 비교, bitwise, shift와 정수 산술 |
| `JUMP` | direct target block 진입 |
| `BRANCH` | canonical bool에 따른 두 direct block 중 하나 |
| `RETURN` | nested frame이면 caller result로 복사, entry면 내부 return slot으로 봉인 |
| `TRAP` | catch 불가능한 `VmFault(INVALID_STATE)` |

String과 symbol token은 target pointer가 아니다. `CONST_STRING`의 8 bytes는 little-endian
`{u32 constant_id, u32 byte_length}`다. 4-byte symbol은 constant ID이고 8-byte
opaque symbol은 같은 ID와 length 쌍이다. 이 값은 source spelling이나 product
semantic object 자체가 아니며 후속 helper/aggregate resolver가 constant table과
expected type을 다시 검사해야 한다.

`CALL_DIRECT`의 explicit frame stack과 loop bound는
{doc}`ribos-bounded-calls-loops-v1`에 따라 실행한다. Aggregate, member/index,
variant와 collection opcode는 {doc}`ribos-bounded-aggregate-runtime-v1`에 따라
실행한다. `CALL_HELPER`는 유효 artifact에 존재해도 이 incremental engine에서
성공하지 않으며 도달하면 `VmFault(INVALID_STATE)`를 봉인한다. Partial opcode
engine을 production policy executor로 해석하거나 boot transfer에 연결할 수 없다.

## Checked integer 의미

모든 integer width는 8, 16, 32 또는 64다. 연산은 host C overflow, signed shift와
implementation-defined right shift에 의존하지 않는다.

| 연산 | 정의 |
| --- | --- |
| add/subtract/multiply | result가 operand type 범위를 벗어나면 `ARITHMETIC` |
| divide/remainder | divisor zero면 `ARITHMETIC` |
| signed min / -1 | divide와 remainder 모두 `ARITHMETIC` |
| signed divide | zero 방향으로 truncate |
| signed remainder | `a == (a / b) * b + a % b`, nonzero 결과 부호는 dividend와 같음 |
| left shift | count가 `[0, width)`이고 수학적 곱이 type 범위 안일 때만 성공 |
| unsigned right shift | zero-fill |
| signed right shift | 음수도 명시적인 floor division by power-of-two, sign-fill 의미 |
| bitwise | exact width의 two's-complement bit pattern |
| unary negative | signed minimum이면 `ARITHMETIC` |
| comparison | 같은 type ID의 canonical value만 비교 |

Invalid width, type mismatch, uninitialized slot과 canonical하지 않은 bool은
`INVALID_VALUE`다. Arithmetic domain failure와 runtime representation failure를
같은 fault로 합치지 않는다.

## Fault receipt

Instruction budget, arithmetic, trap, unsupported opcode와 invariant failure는
catchable language exception이 아니다. Interpreter는 160-byte arena fault region에
다음을 explicit little-endian으로 한 번만 봉인한다.

- stable fault code와 subject
- function/instruction ID
- source-map detail
- consumed instruction count
- artifact hash
- zero helper/effect/duration fields

Public `RibosVmFaultReceipt`는 native structure image를 arena에 저장하지 않고 fixed
record를 field별로 decode한 복사본이다. 두 번째 fault seal과 terminal 뒤 재실행은
거부한다.

## Gate

```sh
make check-ribos-vm-scalar
```

Gate는 tracked `.rbs`를 artifact로 만들고 authorization과 독립 verifier 두 단계를
통과시킨 뒤 다음을 검사한다.

- parameter, bool/integer/string/symbol constant와 move 실행
- 모든 integer width의 unary/binary scalar 경로
- direct jump와 branch가 계산한 true 경로
- step과 bounded run API
- context byte mutation이 fuel을 소비하지 않는 fail-closed rejection
- fuel `N`과 `N-1`, zero-before-dispatch receipt
- unsigned overflow·underflow·multiply overflow
- divide-by-zero와 invalid shift count
- signed minimum negation과 `INT64_MIN / -1`
- `RETURN`, compiler-emitted unreachable `TRAP` handler
- fault instruction/source/counter/artifact provenance
- target archive에 host allocator와 architecture/product branch가 없는지

Direct call과 loop resource closure의 focused 증거는 각각
`make check-ribos-vm-calls`와 `make check-ribos-vm-loops`가 소유한다.
